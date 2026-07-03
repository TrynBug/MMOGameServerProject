using System.Collections;
using UnityEngine;
using Client.Network;

namespace Client.Game
{
    // 1명의 캐릭터(내 캐릭터 또는 다른 유저)를 표현하는 컴포넌트.
    // LocalPlayer 는 StageManager.EnsureLocalPlayer 가 1회 생성해 영속 보관(스테이지 이동 시 숨김/재배치).
    // 타 유저는 ObjectVisibilityNtf 수신 시 동적으로 생성/제거한다.
    // ActorObject 를 상속하여 스탯(StatHolder)과 현재 HP/MP 를 갖는다.
    //
    // 이동 로직은 두 헬퍼로 분리되어 있다(가독성/테스트성):
    //   - LocalPlayerMover   : 본인 클라 예측 이동(목적지/경로/스킬강제이동/회전/잠금)
    //   - PlayerReconciler   : 본인 서버 화해(RTT/입력리플레이/오차보정)
    // 이 클래스는 정체성 + 애니메이션 + 원격(타 유저) 보간을 담당하고, 위 둘을 소유해 위임한다.
    public class PlayerCharacter : ActorObject
    {
        // 오브젝트 식별자 (= characterId). MonsterObject.ObjectId 와 동일 체계.
        public long ObjectId { get; private set; }
        public string CharacterName { get; private set; }

        // 내 캐릭터 여부 (다른 유저와 구분하기 위해)
        public bool IsLocalPlayer { get; private set; }

        // 이동속도 오버라이드 (에디터 전용)
        [SerializeField] private bool m_useMoveSpeedOverride = false;
        [SerializeField] private float m_moveSpeedOverride = 5f;
        [SerializeField] private float m_rotateSpeedDeg = 720f;      // 초당 회전 각도. 720 = 0.5초에 한바퀴.

        // ── 이동 헬퍼 (본인 전용) ──────────────────────────────────
        private LocalPlayerMover m_mover;
        private PlayerReconciler m_reconciler;

        // 캐릭터가 현재 이동 중인지 (본인=예측 이동상태, 원격=보간 스냅샷 플래그).
        public bool IsMoving => IsLocalPlayer ? m_mover.IsMoving : m_remoteMoving;
        public bool IsSkillMoving => m_mover != null && m_mover.IsSkillMoving;

        // ── 원격 캐릭터 모션 드라이버 ─────────────────────────────────
        // 타 유저(IsLocalPlayer=false)는 navmesh 재현 대신 IRemoteMotionDriver 로 재생한다(현재: 스냅샷 보간).
        // 본인(LocalPlayer)은 예측이 우선이라 이 드라이버를 쓰지 않는다(자기 위치는 화해로 보정).
        private readonly ISnapshotMotionDriver m_motion = new SnapshotInterpolator();
        private bool m_remoteMoving;

        // ─── Animator ────────────────────────────────────────────────
        // Visual 하위에 부착된 Animator. Awake 에서 찾아둠.
        // prefab 에 모델이 없거나 Animator 가 없으면 null 일 수 있음 (이 경우 애니메이션 없이 동작).
        private Animator m_animator;
        // Animator 파라미터 hash. 문자열 lookup 을 피해 성능 약간 좋음.
        private static readonly int s_paramSpeed = Animator.StringToHash("Speed");
        // Speed 파라미터 보간 시간(초). 이 시간 동안 idle↔walk↔run 블렌드를 거친다.
        private const float k_speedDampTime = 0.12f;
        // 시전 애니 재생속도 멀티플라이어. cast 스테이트의 Speed 가 이 파라미터를 Multiplier 로 써야 적용됨.
        private static readonly int s_paramCastSpeed = Animator.StringToHash("CastSpeed");
        // 이동시전(Mobile)용 상반신 오버라이드 레이어 이름/트리거. (베이스 Cast 트리거와 분리)
        private const string k_upperBodyLayerName = "UpperBody";
        private const string k_castUpperTrigger = "CastUpper";
        private int m_upperBodyLayer = -2;   // -2 = 미해결, -1 = 레이어 없음
        private Coroutine m_upperFade;

        private void Awake()
        {
            // Visual 하위 어딘가에 있는 Animator 를 찾음. prefab 구조상 PlayerCharacter > Visual > Kiki-v2 에 부착되어 있음.
            m_animator = GetComponentInChildren<Animator>();
            if (m_animator == null)
            {
                Debug.LogWarning($"[PlayerCharacter] Animator 를 찾지 못했습니다. 애니메이션 없이 동작합니다.");
            }

            m_mover = new LocalPlayerMover(transform, m_rotateSpeedDeg);
            m_reconciler = new PlayerReconciler();
        }

        // 스킬 시전 애니메이션 1회 재생. triggerName = Animator 의 Trigger 파라미터 이름(Skill 데이터 CastAnim).
        // 빈 문자열이거나 Animator 가 없으면 무시한다.
        // castSpeed(= 시전클립 기본길이 / ActionLock초)를 받아 CastSpeed 파라미터에 넣어
        // 시전 애니가 ActionLock 길이에 맞게 재생되도록 한다.
        public void PlayCastAnimation(string triggerName, float castSpeed)
        {
            if (m_animator == null || string.IsNullOrEmpty(triggerName))
                return;

            // CastSpeed 를 먼저 세팅한 뒤 트리거 → cast 스테이트가 첫 프레임부터 맞는 속도로 재생.
            m_animator.SetFloat(s_paramCastSpeed, Mathf.Max(0.01f, castSpeed));
            // 트리거 파라미터가 있을 때만 발동(신형 by-name 컨트롤러엔 트리거가 없어 경고 방지).
            if (AnimPlay.HasParam(m_animator, Animator.StringToHash(triggerName)))
                m_animator.SetTrigger(triggerName);
        }

        // ─── 원샷/감정표현/전투 재생 (LayerLabCharacter.controller, by-name) ────
        // 새 캐릭터 컨트롤러의 상태를 이름으로 재생한다. 상태가 없으면(구 컨트롤러/모델 없음) 조용히 무시.
        // 로컬/원격 공용: 로컬은 입력·예측으로, 원격은 서버 이벤트로 호출한다.

        private string m_activeOneShot;      // 현재 재생 중 원샷 상태명(null=없음)
        private bool m_oneShotCancelOnMove;  // 이동 시작 시 취소할지
        private bool m_stunned;
        private float m_lastHitTime = -999f; // 피격 애니 쓰로틀용
        private const float k_hitThrottleSec = 0.4f;

        // 점프. 이동 중에도 위치는 Mover 가 계속 움직이므로 "점프 모션 + 이동" 이 동시에 표현된다(이동해도 취소 안 함).
        public void PlayJump() => playOneShot(AnimStates.Jump, cancelOnMove: false);

        // 감정표현(Dance/Emoji). idle 에서만 호출 권장. 이동 시작 시 자동 취소.
        public void PlayEmote(string emoteState) => playOneShot(emoteState, cancelOnMove: true);

        // 피격. Idle/Walk/Run(Locomotion) 중일 때만 재생하고, 쓰로틀로 연속 피격 시 반복을 억제한다.
        // (캐스팅/점프/공격/스턴/사망 중에는 Locomotion 이 아니므로 자동 스킵 → 시전/동작이 안 끊긴다.)
        public void PlayHit()
        {
            if (m_animator == null) return;
            if (Time.time - m_lastHitTime < k_hitThrottleSec) return;
            if (!AnimPlay.IsCurrent(m_animator, AnimStates.Locomotion)) return;
            m_lastHitTime = Time.time;
            playOneShot(AnimStates.GetHit, cancelOnMove: false);
        }

        // 캐스팅(홀드): castSpeed 로 시전길이에 맞춘 뒤 Cast 상태 진입(마지막 프레임 정지).
        public void PlayCast(string castState, float castSpeed)
        {
            if (m_animator == null || string.IsNullOrEmpty(castState)) return;
            AnimPlay.SetFloatSafe(m_animator, AnimStates.HCastSpeed, Mathf.Max(0.01f, castSpeed));
            AnimPlay.CrossFade(m_animator, castState);
            m_activeOneShot = null;
        }

        // 발동(원샷 → 복귀).
        public void PlayFire(string fireState, float castSpeed)
        {
            if (m_animator == null || string.IsNullOrEmpty(fireState)) return;
            AnimPlay.SetFloatSafe(m_animator, AnimStates.HCastSpeed, Mathf.Max(0.01f, castSpeed));
            playOneShot(fireState, cancelOnMove: false);
        }

        // 스턴/속박 on/off. on: Stun 루프 진입, off: Locomotion 복귀.
        public void SetStunned(bool stunned)
        {
            if (m_animator == null || m_stunned == stunned) return;
            m_stunned = stunned;
            AnimPlay.CrossFade(m_animator, stunned ? AnimStates.Stun : AnimStates.Locomotion);
        }

        // 사망 연출 재생 / 끝포즈 고정.
        public void PlayDeadState() { if (m_animator != null) AnimPlay.CrossFade(m_animator, AnimStates.Dead); }
        public void SetDeadPose()   { if (m_animator != null) AnimPlay.PlayPose(m_animator, AnimStates.Dead, 1f); }

        private void playOneShot(string state, bool cancelOnMove)
        {
            if (m_animator == null) return;
            if (!AnimPlay.CrossFade(m_animator, state)) return;
            m_activeOneShot = state;
            m_oneShotCancelOnMove = cancelOnMove;
        }

        // 이동시전(Mobile): 상반신 레이어 가중치를 올리고 상반신 시전 트리거를 쏜다.
        // 베이스 레이어는 locomotion 을 유지하므로 다리는 계속 달린다. durationSec 뒤 가중치를 0 으로 페이드.
        public void PlayCastUpperBody(float castSpeed, float durationSec)
        {
            if (m_animator == null)
                return;

            m_animator.SetFloat(s_paramCastSpeed, Mathf.Max(0.01f, castSpeed));

            int layer = resolveUpperBodyLayer();
            if (layer < 0)
                return;   // 상반신 레이어 없음 → 미구성. (이동시전 스킬엔 UpperBody 레이어가 필요)

            m_animator.SetLayerWeight(layer, 1f);
            m_animator.SetTrigger(k_castUpperTrigger);

            if (m_upperFade != null)
                StopCoroutine(m_upperFade);
            m_upperFade = StartCoroutine(fadeUpperBodyOut(layer, durationSec));
        }

        private int resolveUpperBodyLayer()
        {
            if (m_upperBodyLayer == -2 && m_animator != null)
                m_upperBodyLayer = m_animator.GetLayerIndex(k_upperBodyLayerName);
            return m_upperBodyLayer;
        }

        // durationSec 동안 유지 후 상반신 레이어 가중치를 0 으로 부드럽게 내린다.
        private IEnumerator fadeUpperBodyOut(int layer, float holdSec)
        {
            if (holdSec > 0f)
                yield return new WaitForSeconds(holdSec);

            const float fadeSec = 0.15f;
            float start = m_animator.GetLayerWeight(layer);
            float t = 0f;
            while (t < fadeSec)
            {
                t += Time.deltaTime;
                m_animator.SetLayerWeight(layer, Mathf.Lerp(start, 0f, t / fadeSec));
                yield return null;
            }
            m_animator.SetLayerWeight(layer, 0f);
            m_upperFade = null;
        }

        // 이동을 seconds 동안 잠그고(Stationary 시전), 현재 이동도 즉시 멈춘다.
        public void LockMovement(float seconds)
        {
            m_mover.LockMovement(seconds);
        }

        // StageManager가 캐릭터 생성 직후 1회 호출
        public void Initialize(long objectId, string name, bool isLocalPlayer, Vector3 pos, float dirY)
        {
            ObjectId = objectId;
            CharacterName = name;
            IsLocalPlayer = isLocalPlayer;

            // 위치/방향 적용
            transform.position = pos;
            transform.rotation = Quaternion.Euler(0f, dirY, 0f);

            // GameObject 이름을 식별 가능하게
            gameObject.name = $"Player_{objectId}_{name}{(isLocalPlayer ? "_LOCAL" : "")}";
        }

        // 서버에서 위치 강제 동기화(스폰/텔레포트/화해 하드스냅) 시 호출. 즉시 적용 + 이동 취소.
        public void SetPosition(Vector3 pos, float dirY)
        {
            transform.position = pos;
            transform.rotation = Quaternion.Euler(0f, dirY, 0f);
            m_mover.CancelForTeleport();
            // 텔포/스폰/하드스냅 후엔 예측 히스토리/잔차를 초기화(이 시점 이전 stale 비교 방지).
            m_reconciler?.Clear();
        }

        // 서버 SnapshotNtf 수신 시 호출(StageManager). 원격 캐릭터만 보간 버퍼에 쌓는다.
        // 본인은 예측이 우선이라 무시한다(자기 위치 화해는 ReconcileTo).
        public void OnSnapshot(double serverTimeMs, Vector3 pos, float yaw, bool moving)
        {
            if (IsLocalPlayer)
                return;
            m_motion.OnSnapshot(serverTimeMs, pos, yaw, moving);
        }

        // 원격 캐릭터 1프레임 재생. 드라이버가 자체 타임라인에서 뽑은 위치/회전을 적용한다.
        private void updateRemoteInterpolation()
        {
            if (m_motion.Sample(out Vector3 pos, out float yaw, out bool moving))
            {
                transform.position = pos;
                transform.rotation = Quaternion.Euler(0f, yaw, 0f);
                m_remoteMoving = moving;
            }
        }

        // PlayerMoveController 가 MoveIntentReq 송신 직후 호출. RTT 측정 + 게이팅용 기록.
        public void NotifyInputSent(uint seq, Vector3 dest, bool isStop)
        {
            m_reconciler.NotifyInputSent(seq, dest, isStop);
        }

        // 본인 캐릭터 화해. 서버 SnapshotNtf 의 본인 항목으로 StageManager 가 호출한다.
        //   snapServerMs = authPos 의 서버시각(= server_tick_seq * 50ms). 시간정렬 비교 기준.
        //   ackSeq       = 서버가 마지막으로 처리한 입력 seq (Phase 2 커맨드 리플레이용, Phase 1 미사용).
        //   authPos      = 그 서버시각 시점의 권위 위치.
        public void ReconcileTo(Vector3 authPos, float authYaw, double snapServerMs, uint ackSeq)
        {
            if (!IsLocalPlayer)
                return;

            PlayerReconciler.ReconcileResult r = m_reconciler.Reconcile(
                transform, snapServerMs, authPos, ackSeq,
                m_mover.IsMoving, m_mover.IsSkillMoving, GetMoveSpeed());

            switch (r)
            {
                case PlayerReconciler.ReconcileResult.Snap:
                    // 의도 없는 미커버(접속/스테이지이동 등) → 권위로 즉시 스냅.
                    SetPosition(authPos, authYaw);
                    break;

                case PlayerReconciler.ReconcileResult.Reconstruct:
                    // 넉백/블링크거부/강제이동 desync → 권위 authPos 에서 내 최신 의도를 RTT 재실행해 현재 재구성.
                    m_mover.ReconstructFrom(authPos, authYaw,
                                            m_reconciler.LatestDest, m_reconciler.LatestIsStop,
                                            m_reconciler.RttMs / 1000f, GetMoveSpeed());
                    m_reconciler.Clear();   // 점프했으므로 예측 히스토리/잔오차 리셋(최신 의도 seq 는 유지).
                    break;

                // None: 소프트(동기화/게이트/데드존). ApplyBleed 가 잔오차 처리.
            }
        }

        // ─── 이동 (위임) ─────────────────────────────────────────────────

        // 플레이어캐릭터 이동속도 얻기 함수
        public float GetMoveSpeed()
        {
            if (m_useMoveSpeedOverride)
                return m_moveSpeedOverride;
            return (float)Stats.Get(GameData.EStat.MoveSpdTotal);
        }

        // 목적지 설정. 마우스 누르고 있는 동안 매 프레임 갱신됨.
        public void SetMoveDestination(Vector3 dest)
        {
            m_mover.SetDestination(dest);
        }

        // 즉시 멈춤. (마우스 버튼을 뗐을 때)
        public void StopMove()
        {
            m_mover.Stop();
        }

        // 스킬 강제이동 시작 (이동기/순간이동). 서버 Character::ApplySkillMovement 와 동일 공식.
        public void StartSkillMove(Vector3 dirFlat, float distance, float durationSec)
        {
            m_mover.StartSkillMove(dirFlat, distance, durationSec);
        }

        private void Update()
        {
            // 원격 캐릭터(타 유저)는 서버 스냅샷을 보간한다 (navmesh 재현 안 함).
            if (!IsLocalPlayer)
            {
                updateRemoteInterpolation();
                updateAnimator();
                return;
            }

            // 본인(LocalPlayer): 클라 예측 이동.
            m_mover.Tick(GetMoveSpeed());

            // 서버 권위와의 위치 오차를 매 프레임 부드럽게 보정(이동 중/정지 모두). 동기화돼 있으면 오차≈0.
            if (!m_mover.IsSkillMoving)
                m_reconciler.ApplyBleed(transform);

            // 시간정렬 화해용: 최종 렌더 위치를 추정 서버시각으로 스탬프해 히스토리에 기록.
            // 스킬 강제이동 중에도 기록해 히스토리에 구멍을 내지 않는다(화해는 skillMoving 가드로 별도 생략).
            if (NetClock.IsReady)
                m_reconciler.RecordPrediction(NetClock.EstServerNowMs(), transform.position);

            // (로컬) 점프 입력 — 이동과 무관하게 점프 모션만 재생한다. 위치는 Mover 가 계속 구동하므로 점프하며 이동한다.
            if (Input.GetKeyDown(KeyCode.Space))
                PlayJump();

            // Animator 갱신은 정지 전환(true -> false)도 잡아야 하므로 매 프레임 호출.
            updateAnimator();
        }

        // Animator 의 Speed(float) 파라미터를 이동 상태와 동기화.
        // 이동 여부를 0/1 목표값으로 두고 damping 으로 보간 → 가/감속 구간에서 walk 블렌드를
        // 거쳐 run 으로 자연스럽게 전환된다. (블렌드 트리: idle=0, walk=0.5, run=1)
        private void updateAnimator()
        {
            if (m_animator == null) return;

            // 본인은 예측 이동상태, 원격은 보간 스냅샷의 이동 플래그를 쓴다.
            bool isMoving = IsLocalPlayer ? m_mover.IsMoving : m_remoteMoving;
            float target = isMoving ? 1f : 0f;
            m_animator.SetFloat(s_paramSpeed, target, k_speedDampTime, Time.deltaTime);

            // 원샷(감정표현 등) 이동취소 / 자연종료 추적.
            if (m_activeOneShot != null)
            {
                if (m_oneShotCancelOnMove && isMoving)
                {
                    // 이동 시작 → 감정표현 등 취소하고 Locomotion 복귀.
                    AnimPlay.CrossFade(m_animator, AnimStates.Locomotion);
                    m_activeOneShot = null;
                }
                else if (AnimPlay.IsCurrent(m_animator, AnimStates.Locomotion))
                {
                    // 원샷이 Has Exit Time 으로 자연 복귀 완료 → 추적 해제.
                    m_activeOneShot = null;
                }
            }
        }
    }
}
