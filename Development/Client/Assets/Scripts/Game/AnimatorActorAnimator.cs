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

        private void Awake()
        {
            m_animator = GetComponentInChildren<Animator>();
            if (m_animator == null)
                Debug.LogWarning($"[AnimatorActorAnimator] Animator 를 찾지 못했습니다. ({name}) 애니메이션 없이 동작합니다.");
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
                }
            }
        }

        // 이동/정지. Speed 목표값 0/1 로 반영(실제 보간은 Update).
        public void SetMoving(bool isMoving)
        {
            if (m_hasLastMoving && m_lastMoving == isMoving) return;
            m_lastMoving = isMoving;
            m_hasLastMoving = true;
            m_targetSpeed = isMoving ? 1f : 0f;
        }

        public void SetSpeed(float speed01)
        {
            m_targetSpeed = speed01;
            m_hasLastMoving = false;   // SetMoving 캐시 무효화(다음 SetMoving 이 반영되도록).
        }

        public void PlayOneShot(string stateName, bool cancelOnMove)
        {
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
            m_activeOneShot = null;   // 스턴 진입/해제 시 진행 중 원샷(감정 등) 추적 해제.
            AnimPlay.CrossFade(m_animator, stunned ? AnimStates.Stun : AnimStates.Locomotion);
        }

        public void PlayCast(string castState, float castSpeed)
        {
            AnimPlay.SetFloatSafe(m_animator, AnimStates.HCastSpeed, Mathf.Max(0.01f, castSpeed));
            AnimPlay.CrossFade(m_animator, castState);
            m_activeOneShot = null;   // 캐스팅(홀드)은 이동취소 원샷 로직 대상 아님.
        }

        public void PlayFire(string fireState, float castSpeed)
        {
            AnimPlay.SetFloatSafe(m_animator, AnimStates.HCastSpeed, Mathf.Max(0.01f, castSpeed));
            PlayOneShot(fireState, cancelOnMove: false);
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
            AnimPlay.CrossFade(m_animator, AnimStates.Dead);
        }

        // 사망 끝 포즈로 즉시 고정. Dead 상태를 normalizedTime=1 로 재생(전이 없이 마지막 프레임).
        public void SetDeadPose()
        {
            m_activeOneShot = null;
            AnimPlay.PlayPose(m_animator, AnimStates.Dead, 1.0f);
        }

        // 강제 Locomotion 복귀(부활 등 특수 상태 탈출).
        public void ReturnToLocomotion()
        {
            m_activeOneShot = null;
            AnimPlay.CrossFade(m_animator, AnimStates.Locomotion);
        }
    }
}
