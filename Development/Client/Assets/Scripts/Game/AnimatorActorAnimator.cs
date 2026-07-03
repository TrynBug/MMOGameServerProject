using UnityEngine;

namespace Client.Game
{
    // IActorAnimator 의 Unity Animator 기반 기본 구현.
    //
    // 같은 GameObject(또는 자식)에 있는 Animator 를 찾아, 의미론적 상태를 Animator 로 변환한다.
    // 아트가 아직 안 붙은 prefab 등으로 Animator 가 없으면 모든 호출을 조용히 무시한다(애니메이션 없이 동작).
    //
    // 몬스터와 (원격) 캐릭터가 같은 컴포넌트를 공유하지만 컨트롤러 구성은 다를 수 있다.
    // 따라서 모든 재생은 AnimPlay 헬퍼로 "상태/파라미터가 있을 때만" 수행한다(없으면 no-op).
    //   - 이동:   Speed(Float) 블렌드
    //   - 원샷:   상태명으로 CrossFade (Jump/GetHit/공격/감정표현 …)
    //   - 사망:   Dead 상태로 CrossFade
    public class AnimatorActorAnimator : MonoBehaviour, IActorAnimator
    {
        private Animator m_animator;

        // 마지막으로 반영한 이동 여부. 매 프레임 중복 세팅을 피하기 위한 캐시.
        private bool m_lastMoving;
        private bool m_hasLastMoving;

        // 현재 재생 중인 원샷 상태(감정표현 등). 이동 시작 시 취소 판단에 사용.
        private string m_activeOneShot;
        private bool m_oneShotCancelOnMove;

        private bool m_stunned;
        private float m_lastHitTime = -999f; // 피격 애니 쓰로틀용
        private const float k_hitThrottleSec = 0.4f;

        private void Awake()
        {
            m_animator = GetComponentInChildren<Animator>();
            if (m_animator == null)
                Debug.LogWarning($"[AnimatorActorAnimator] Animator 를 찾지 못했습니다. ({name}) 애니메이션 없이 동작합니다.");
        }

        private void Update()
        {
            // 원샷이 자연 종료되어 Locomotion 으로 돌아왔으면 추적 해제.
            if (m_activeOneShot != null && AnimPlay.IsCurrent(m_animator, AnimStates.Locomotion))
                m_activeOneShot = null;
        }

        // 이동/정지를 Speed(0/1) 로 반영.
        public void SetMoving(bool isMoving)
        {
            if (m_hasLastMoving && m_lastMoving == isMoving)
                return;
            m_lastMoving = isMoving;
            m_hasLastMoving = true;

            SetSpeed(isMoving ? 1f : 0f);
        }

        public void SetSpeed(float speed01)
        {
            AnimPlay.SetFloatSafe(m_animator, AnimStates.HSpeed, speed01);

            // 이동 시작으로 취소돼야 하는 원샷(감정표현 등)이 있으면 Locomotion 복귀.
            if (m_activeOneShot != null && m_oneShotCancelOnMove && speed01 > 0.01f)
            {
                AnimPlay.CrossFade(m_animator, AnimStates.Locomotion);
                m_activeOneShot = null;
            }
        }

        public void PlayOneShot(string stateName, bool cancelOnMove)
        {
            if (!AnimPlay.CrossFade(m_animator, stateName))
                return;
            m_activeOneShot = stateName;
            m_oneShotCancelOnMove = cancelOnMove;
        }

        // 피격. Idle/Walk/Run(Locomotion) 중일 때만 재생하고, 쓰로틀로 연속 피격 시 반복을 억제한다.
        // (공격/사망/스턴 등 Locomotion 이 아닌 상태에서는 자동 스킵.)
        public void PlayHit()
        {
            if (m_animator == null) return;
            if (Time.time - m_lastHitTime < k_hitThrottleSec) return;
            if (!AnimPlay.IsCurrent(m_animator, AnimStates.Locomotion)) return;
            m_lastHitTime = Time.time;
            PlayOneShot(AnimStates.GetHit, cancelOnMove: false);
        }

        public void SetStunned(bool stunned)
        {
            if (m_stunned == stunned)
                return;
            m_stunned = stunned;
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
        // (Cast_Magic 은 '발동 대기 홀드' 상태라 몬스터의 1회 공격엔 부적합 → Melee_1 사용.
        //  Slime_Override 등에서 Melee_1 슬롯을 몬스터 공격 클립으로 오버라이드한다.)
        public void PlaySkill()
        {
            PlayOneShot(AnimStates.Melee1, cancelOnMove: false);
        }

        // 사망 연출 재생. Dead 상태로 CrossFade.
        public void PlayDead()
        {
            AnimPlay.CrossFade(m_animator, AnimStates.Dead);
        }

        // 사망 끝 포즈로 즉시 고정. Dead 상태를 normalizedTime=1 로 재생(전이 없이 마지막 프레임).
        public void SetDeadPose()
        {
            AnimPlay.PlayPose(m_animator, AnimStates.Dead, 1.0f);
        }
    }
}
