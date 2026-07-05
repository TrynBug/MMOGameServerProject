using UnityEngine;

namespace Client.Game
{
    // IActorAnimator 의 Unity Animator 기반 구현. 플레이어(로컬/원격)·몬스터 공용 애니메이션 드라이버.
    //
    // 같은 GameObject(또는 자식)에 있는 Animator 를 찾아, 의미론적 상태를 Animator 로 변환한다.
    // Animator 가 없으면 모든 호출을 조용히 무시한다. 컨트롤러 구성이 달라도 AnimPlay 로 "상태/파라미터가
    // 있을 때만" 재생하므로(없으면 no-op) 어떤 컨트롤러(캐릭터/Slime/Goblin override)에도 안전하다.
    //   - 이동:   Speed(Float) 블렌드 (Update 에서 damping)
    //   - 원샷:   상태명으로 CrossFade (Jump/GetHit/공격/감정표현/발동 …). 감정표현은 이동 시 자동 취소.
    //   - 캐스팅: Cast 상태 홀드 → 발동 시 Fire 로 전환. 사망: Dead 상태.
    //
    // 소유: 몬스터는 MonsterFactory 가 런타임 AddComponent. 플레이어는 PlayerCharacter.Awake 가 AddComponent 후 위임.
    public class AnimatorActorAnimator : MonoBehaviour, IActorAnimator
    {
        private Animator m_animator;

        // 이동 블렌드 목표 Speed(0~1). Update 에서 damping 으로 보간한다.
        private float m_targetSpeed;
        private const float k_speedDampTime = 0.12f;

        // 이동 여부 캐시(SetMoving 중복 방지).
        private bool m_lastMoving;
        private bool m_hasLastMoving;

        // 현재 재생 중인 원샷 상태. 이동취소 / 자연종료 추적.
        private string m_activeOneShot;
        private bool m_oneShotCancelOnMove;   // true = 감정표현(이동 시 취소)
        private bool m_oneShotEntered;        // 원샷 상태에 실제 진입했는지(크로스페이드 완료). 자연종료 오판 방지.

        private bool m_stunned;
        private float m_lastHitTime = -999f;
        private const float k_hitThrottleSec = 0.4f;

        // ── 하체 오버라이드 레이어 ("LowerBody") ───────────────────────
        // 스킬 시전/발동 중(m_lowerBodyOverride) 이동하면 다리만 Walk/Run 으로 오버라이드하고
        // 상체는 base 레이어의 시전/발동 모션을 유지한다. 레이어가 없는 컨트롤러(-1)에선 비활성.
        private int   m_lowerLayerIndex = -1;
        private bool  m_lowerBodyOverride;
        private float m_lowerWeight;
        private const float k_lowerBlendTime = 0.12f;

        private void Awake()
        {
            m_animator = GetComponentInChildren<Animator>();
            if (m_animator == null)
                Debug.LogWarning($"[AnimatorActorAnimator] Animator 를 찾지 못했습니다. ({name}) 애니메이션 없이 동작합니다.");
            else
                m_lowerLayerIndex = m_animator.GetLayerIndex("LowerBody");   // 없으면 -1
        }

        private void Update()
        {
            if (m_animator == null) return;

            // 이동 블렌드 damping (idle↔walk↔run 부드럽게).
            AnimPlay.SetFloatDamped(m_animator, AnimStates.HSpeed, m_targetSpeed, k_speedDampTime, Time.deltaTime);

            // 원샷(감정표현 등) 이동취소 / 자연종료 추적.
            if (m_activeOneShot != null)
            {
                if (m_oneShotCancelOnMove && m_targetSpeed > 0.01f)
                {
                    // 이동 시작 → 감정표현 등 취소하고 Locomotion 복귀.
                    AnimPlay.CrossFade(m_animator, AnimStates.Locomotion);
                    m_activeOneShot = null;
                }
                else if (AnimPlay.IsCurrent(m_animator, m_activeOneShot))
                {
                    // 원샷 상태에 실제 진입함(크로스페이드 완료).
                    m_oneShotEntered = true;
                }
                else if (m_oneShotEntered && AnimPlay.IsCurrent(m_animator, AnimStates.Locomotion))
                {
                    // 진입 후 Locomotion 복귀(원샷 자연종료) → 추적 해제.
                    // ※ 진입 전(크로스페이드 프레임)엔 Animator 가 아직 이전(Locomotion) 상태를 반환하므로,
                    //    Entered 가드 없이 해제하면 감정 진입 직후 잘못 해제되어 이동취소가 안 먹힌다.
                    m_activeOneShot = null;
                    m_lowerBodyOverride = false;   // 발동 원샷 자연종료 → 하체 오버라이드 해제.
                }
            }

            // 하체 오버라이드 weight: 스킬 시전/발동 중(m_lowerBodyOverride)이면 항상 1로 둔다.
            // idle↔run 은 Speed 파라미터(LowerLocomotion 블렌드트리)가 담당하므로, weight 를 이동여부로 껐다 켜지 않는다.
            //   - 잠금 중(정지): weight 1 + Speed 0 → 다리 Idle, 상체 발동 (마법 시전은 다리 고정이라 전신과 사실상 동일).
            //   - 잠금 끝나고 이동: weight 는 이미 1 → Speed 즉시스냅으로 다리가 곧바로 Run. weight 페이드인이 없어
            //     "발동애니가 잠금보다 긴 스킬(얼음지대 등)"에서 이동 시작 시 다리가 뒤처져 튀어나가던 현상 제거.
            if (m_lowerLayerIndex >= 0)
            {
                float target = m_lowerBodyOverride ? 1f : 0f;
                m_lowerWeight = Mathf.MoveTowards(m_lowerWeight, target, Time.deltaTime / k_lowerBlendTime);
                m_animator.SetLayerWeight(m_lowerLayerIndex, m_lowerWeight);
            }
        }

        // 이동/정지. Speed 목표값 0/1 로 반영(실제 보간은 Update).
        public void SetMoving(bool isMoving)
        {
            if (m_hasLastMoving && m_lastMoving == isMoving) return;
            m_lastMoving = isMoving;
            m_hasLastMoving = true;
            m_targetSpeed = isMoving ? 1f : 0f;

            // 이동 시작은 램프 없이 즉시 Run 으로 스냅한다. 위치는 첫 프레임부터 풀스피드로 움직이는데
            // (LocalPlayerMover) 애니만 0→walk→run 으로 램프하면 다리가 뒤처져 "앞으로 튀어나가는" 미끄러짐이 생긴다.
            // 정지(→0)는 아래 Update 의 damp 로 부드럽게 감쇠. SetSpeed(중간값) 경로는 영향 없음.
            if (isMoving)
                AnimPlay.SetFloatSafe(m_animator, AnimStates.HSpeed, 1f);
        }

        public void SetSpeed(float speed01)
        {
            m_targetSpeed = speed01;
            m_hasLastMoving = false;   // SetMoving 캐시 무효화(다음 SetMoving 이 반영되도록).
        }

        public void PlayOneShot(string stateName, bool cancelOnMove)
        {
            m_lowerBodyOverride = false;   // 일반 원샷(점프/감정/피격/멜리)은 전신. (PlayFire 가 이후 다시 켠다.)
            if (!AnimPlay.CrossFade(m_animator, stateName)) return;
            m_activeOneShot = stateName;
            m_oneShotCancelOnMove = cancelOnMove;
            m_oneShotEntered = false;   // 아직 크로스페이드 중 → 진입 전.
        }

        // 피격. Locomotion(Idle/Walk/Run) 또는 감정표현 중일 때만 재생 + 쓰로틀로 연속 피격 억제.
        // (캐스팅/점프/공격/스턴/사망 중엔 스킵. 감정표현은 피격으로 끊고 넘어가야 하므로 허용 — 몬스터는 감정이 없어 사실상 Locomotion-only.)
        public void PlayHit()
        {
            if (m_animator == null) return;
            if (Time.time - m_lastHitTime < k_hitThrottleSec) return;
            bool inLocomotion = AnimPlay.IsCurrent(m_animator, AnimStates.Locomotion);
            bool inEmote = m_activeOneShot != null && m_oneShotCancelOnMove;
            if (!inLocomotion && !inEmote) return;
            m_lastHitTime = Time.time;
            PlayOneShot(AnimStates.GetHit, cancelOnMove: false);
        }

        public void SetStunned(bool stunned)
        {
            if (m_stunned == stunned) return;
            m_stunned = stunned;
            m_activeOneShot = null;        // 스턴 진입/해제 시 진행 중 원샷(감정 등) 추적 해제.
            m_lowerBodyOverride = false;   // 스턴은 전신.
            AnimPlay.CrossFade(m_animator, stunned ? AnimStates.Stun : AnimStates.Locomotion);
        }

        public void PlayCast(string castState, float castSpeed)
        {
            AnimPlay.SetFloatSafe(m_animator, AnimStates.HCastSpeed, Mathf.Max(0.01f, castSpeed));
            AnimPlay.CrossFade(m_animator, castState);
            m_activeOneShot = null;        // 캐스팅(홀드)은 이동취소 원샷 로직 대상 아님.
            m_lowerBodyOverride = true;    // 시전~발동 동안 하체 오버라이드 허용(실제 적용은 이동할 때만).
        }

        public void PlayFire(string fireState, float castSpeed)
        {
            AnimPlay.SetFloatSafe(m_animator, AnimStates.HCastSpeed, Mathf.Max(0.01f, castSpeed));
            PlayOneShot(fireState, cancelOnMove: false);   // 내부에서 m_lowerBodyOverride 를 false 로 리셋하므로
            m_lowerBodyOverride = true;                    // 그 뒤 다시 켠다(발동 모션 중 이동 시 하체 분리).
        }

        // 공격/시전 1회. 1회성 공격 상태(Melee_1)로 재생 → 종료 시 Locomotion 복귀.
        // (Cast_Magic 은 발동 대기 홀드라 몬스터 1회 공격엔 부적합. Slime_Override 등이 Melee_1 슬롯을 몬스터 공격으로 교체.)
        public void PlaySkill()
        {
            PlayOneShot(AnimStates.Melee1, cancelOnMove: false);
        }

        // 사망 연출 재생. Dead 상태로 CrossFade.
        public void PlayDead()
        {
            m_activeOneShot = null;
            m_lowerBodyOverride = false;
            AnimPlay.CrossFade(m_animator, AnimStates.Dead);
        }

        // 사망 끝 포즈로 즉시 고정. Dead 상태를 normalizedTime=1 로 재생(전이 없이 마지막 프레임).
        public void SetDeadPose()
        {
            m_activeOneShot = null;
            m_lowerBodyOverride = false;
            AnimPlay.PlayPose(m_animator, AnimStates.Dead, 1.0f);
        }

        // 강제 Locomotion 복귀(부활 등 특수 상태 탈출).
        public void ReturnToLocomotion()
        {
            m_activeOneShot = null;
            m_lowerBodyOverride = false;
            AnimPlay.CrossFade(m_animator, AnimStates.Locomotion);
        }
    }
}
