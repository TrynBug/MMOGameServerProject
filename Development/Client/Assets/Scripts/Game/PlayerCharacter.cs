using UnityEngine;
using Client.Network;
using Common;
using GamePacket;

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

        // ─── Animator (공용 드라이버 위임) ───────────────────────────
        // 애니 재생은 공용 드라이버 AnimatorActorAnimator(IActorAnimator)에 위임한다(몬스터와 동일 드라이버 — 로직 중복 제거).
        // PlayerCharacter 는 "이동 여부"만 계산해 넘기고 의미 메서드(PlayJump/PlayCast/…)로 호출한다.
        // Awake 에서 Animator 가 있으면 드라이버를 부착. 모델/Animator 가 없으면 null(애니 없이 동작).
        private IActorAnimator m_actorAnimator;

        private void Awake()
        {
            // Visual 하위의 Animator 를 찾아 공용 애니 드라이버를 부착(없으면 애니 없이 동작).
            m_actorAnimator = GetComponentInChildren<IActorAnimator>();
            if (m_actorAnimator == null)
            {
                Animator anim = GetComponentInChildren<Animator>();
                if (anim != null)
                    m_actorAnimator = gameObject.AddComponent<AnimatorActorAnimator>();
                else
                    Debug.LogWarning($"[PlayerCharacter] Animator 를 찾지 못했습니다. 애니메이션 없이 동작합니다.");
            }

            m_mover = new LocalPlayerMover(transform, m_rotateSpeedDeg);
            m_reconciler = new PlayerReconciler();
        }

        // ─── 애니 재생 (드라이버 위임) ─────────────────────────────────
        // 로컬은 입력·예측으로, 원격은 서버 이벤트로 호출한다. (SkillSystem/StageManager/UI 에서 사용)
        // 게이트(피격 Locomotion/감정), 쓰로틀, 이동취소, 캐스팅 홀드, 원샷 자연종료 추적은 전부 드라이버가 처리.

        // 점프. 이동 중에도 위치는 Mover 가 계속 움직이므로 "점프 모션 + 이동" 이 동시에 표현된다(이동해도 취소 안 함).
        public void PlayJump() => m_actorAnimator?.PlayOneShot(AnimStates.Jump, cancelOnMove: false);

        // 감정표현(Dance/Emoji). 순수 재생(원격 재생 겸용). 로컬 입력은 LocalEmote 로 서버에도 통보.
        public void PlayEmote(string emoteState) => m_actorAnimator?.PlayOneShot(emoteState, cancelOnMove: true);

        // (로컬) 감정표현 시작: 재생 + 서버 통보(관전자 relay).
        public void LocalEmote(string emoteState)
        {
            PlayEmote(emoteState);
            sendActorAction(ActorAction.Emote, emoteState);
        }

        // 코스메틱 액션(점프/감정)을 서버에 통보 → 서버가 AOI relay → 원격 클라 재생. 로컬 전용.
        private void sendActorAction(int actionId, string param)
        {
            NetworkManager net = NetworkManager.Instance;
            if (net == null || !net.IsConnected) return;
            net.Send(GamePacketId.ActorActionReq, new ActorActionReq { ActionId = actionId, Param = param ?? "" });
        }

        // 피격 / 시전(캐스팅→발동) / 스턴 — 드라이버 위임.
        public void PlayHit() => m_actorAnimator?.PlayHit();
        public void PlayCast(string castState, float castSpeed) => m_actorAnimator?.PlayCast(castState, castSpeed);
        public void PlayFire(string fireState, float castSpeed) => m_actorAnimator?.PlayFire(fireState, castSpeed);
        public void SetStunned(bool stunned) => m_actorAnimator?.SetStunned(stunned);

        // 사망 연출 재생 / 끝포즈 고정.
        public void PlayDeadState() => m_actorAnimator?.PlayDead();
        public void SetDeadPose()   => m_actorAnimator?.SetDeadPose();

        // 사망 처리. 서버 ObjectDeathNtf 수신 시 StageManager 가 호출(로컬/원격 공용).
        // 사망 애니 + (로컬) 예측 이동 중단. 입력 차단은 행동 메서드가 IsDead 로 막는다.
        public override void OnDeath()
        {
            if (IsDead) return;      // 멱등
            base.OnDeath();          // IsDead = true (ActorObject)
            if (IsLocalPlayer)
                StopMove();          // 예측 이동 즉시 중단
            m_actorAnimator?.PlayDead();
        }

        // 부활. 사망 해제 + 자원 복원(base) + 애니 Locomotion 복귀. 입력은 IsDead 게이트가 자동 재개.
        // 위치 재배치는 부활 핸들러가 SetPosition 으로 별도 적용한다(예측/화해 히스토리 리셋 포함).
        public override void Revive(double hp, double mp)
        {
            base.Revive(hp, mp);     // IsDead = false, HP/MP 복원
            m_actorAnimator?.ReturnToLocomotion();
        }

        // corpse 상태로 늦게 시야 진입(원격 캐릭터가 부활 대기 중)한 경우: 사망 상태로 들어가되 애니 없이 끝 포즈 고정.
        // MonsterObject.SpawnAsCorpse 와 동형. IsDead 를 세팅한다.
        public void SpawnAsCorpse()
        {
            if (IsDead) return;
            base.OnDeath();       // IsDead = true (ActorObject)
            m_actorAnimator?.SetDeadPose();
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
            if (IsDead) return;   // 사망 중 이동 입력 차단
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

            // (로컬) 점프 입력 — 점프 모션 재생 + 서버 통보(관전자 relay). 위치는 Mover 가 계속 구동하므로 점프하며 이동한다.
            if (!IsDead && Input.GetKeyDown(KeyCode.Space))
            {
                PlayJump();
                sendActorAction(ActorAction.Jump, "");
            }

            // Animator 갱신은 정지 전환(true -> false)도 잡아야 하므로 매 프레임 호출.
            updateAnimator();
        }

        // 이동 여부를 드라이버에 전달. Speed 블렌드 damping + 감정표현 이동취소/자연종료 추적은 드라이버가 처리한다.
        // 본인은 예측 이동상태, 원격은 보간 스냅샷의 이동 플래그를 쓴다.
        private void updateAnimator()
        {
            bool isMoving = IsLocalPlayer ? m_mover.IsMoving : m_remoteMoving;
            m_actorAnimator?.SetMoving(isMoving);
        }
    }
}
