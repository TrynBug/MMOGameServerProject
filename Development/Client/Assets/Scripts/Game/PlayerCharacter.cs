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
        // 캐릭터 식별자
        public long UserId { get; private set; }
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
            m_animator.SetTrigger(triggerName);
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
        public void Initialize(long userId, string name, bool isLocalPlayer, Vector3 pos, float dirY)
        {
            UserId = userId;
            CharacterName = name;
            IsLocalPlayer = isLocalPlayer;

            // 위치/방향 적용
            transform.position = pos;
            transform.rotation = Quaternion.Euler(0f, dirY, 0f);

            // GameObject 이름을 식별 가능하게
            gameObject.name = $"Player_{userId}_{name}{(isLocalPlayer ? "_LOCAL" : "")}";
        }

        // 서버에서 위치 강제 동기화(스폰/텔레포트/화해 하드스냅) 시 호출. 즉시 적용 + 이동 취소.
        public void SetPosition(Vector3 pos, float dirY)
        {
            transform.position = pos;
            transform.rotation = Quaternion.Euler(0f, dirY, 0f);
            m_mover.CancelForTeleport();
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
        //   ackSeq  = 서버가 마지막으로 처리한 입력 seq
        //   authPos = 그 시점(≈RTT 과거)의 서버 권위 위치
        public void ReconcileTo(Vector3 authPos, float authYaw, uint ackSeq)
        {
            if (!IsLocalPlayer)
                return;

            // 큰 desync 면 reconciler 가 true 를 돌려준다 → authPos 로 즉시 스냅.
            if (m_reconciler.Reconcile(transform, authPos, ackSeq,
                                       m_mover.IsMoving, m_mover.IsSkillMoving, GetMoveSpeed()))
                SetPosition(authPos, authYaw);
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
        }
    }
}
