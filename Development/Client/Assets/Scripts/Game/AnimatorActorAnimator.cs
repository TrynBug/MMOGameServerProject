using UnityEngine;

namespace Client.Game
{
    // IActorAnimator 의 Unity Animator 기반 기본 구현.
    //
    // 같은 GameObject(또는 자식)에 있는 Animator 를 찾아, 의미론적 상태를 Animator 파라미터로 변환한다.
    // 아트가 아직 안 붙은 prefab 등으로 Animator 가 없으면 모든 호출을 조용히 무시한다(애니메이션 없이 동작).
    //
    // 몬스터와 캐릭터가 같은 컴포넌트를 공유한다. 따라서 Animator 그래프의 파라미터 규약도 공유한다:
    //   - IsMoving (Bool) : idle <-> move(run/walk) 전환
    // (PlayerCharacter 가 이미 쓰고 있는 "IsMoving" 규약과 동일하게 맞춤.)
    public class AnimatorActorAnimator : MonoBehaviour, IActorAnimator
    {
        // Animator 파라미터 hash. 문자열 lookup 을 피한다.
        private static readonly int s_paramIsMoving = Animator.StringToHash("IsMoving");
        private static readonly int s_paramDead     = Animator.StringToHash("Dead");
        private static readonly int s_paramSkill    = Animator.StringToHash("Skill");

        private Animator m_animator;

        // 마지막으로 Animator 에 보낸 IsMoving 값. 매 프레임 SetBool 호출을 피하기 위한 캐시.
        // 기본값 false 는 Animator 의 IsMoving 기본값(false)과 일치한다.
        private bool m_lastSentIsMoving;

        private void Awake()
        {
            m_animator = GetComponentInChildren<Animator>();
            if (m_animator == null)
                Debug.LogWarning($"[AnimatorActorAnimator] Animator 를 찾지 못했습니다. ({name}) 애니메이션 없이 동작합니다.");
        }

        // 이동/정지 상태를 IsMoving 파라미터로 반영. 값이 바뀐 시점에만 SetBool 호출.
        public void SetMoving(bool isMoving)
        {
            if (m_animator == null)
                return;

            if (m_lastSentIsMoving == isMoving)
                return;

            m_animator.SetBool(s_paramIsMoving, isMoving);
            m_lastSentIsMoving = isMoving;
        }

        // 사망 연출 재생. Animator 에 "Dead" 트리거 파라미터가 있을 때만 발동한다.
        // 아트가 아직 사망 클립/트리거를 안 붙인 prefab 이면 조용히 무시한다(애니 없이 동작).
        public void PlayDead()
        {
            if (m_animator == null)
                return;
            if (!hasParameter(s_paramDead))
                return;

            m_animator.SetTrigger(s_paramDead);
        }

        // 사망 끝 포즈로 즉시 고정. "Dead" 상태(Base 레이어)를 normalizedTime=1 로 재생해 마지막 프레임을 보인다.
        // SetTrigger 와 달리 전이를 거치지 않고 즉시 해당 지점으로 점프한다(애니메이션 재생 없음).
        // "Dead" 상태가 없는 컨트롤러면 Animator 가 경고를 내지만 동작에는 지장이 없다(애니 없이 동작).
        public void SetDeadPose()
        {
            if (m_animator == null)
                return;

            m_animator.Play(s_paramDead, 0, 1.0f);
        }

        // 시전(윈드업) 연출 재생. Animator 에 "Skill" 트리거 파라미터가 있을 때만 발동한다.
        // 아트가 아직 시전 클립/트리거를 안 붙인 prefab 이면 조용히 무시한다(애니 없이 동작).
        public void PlaySkill()
        {
            if (m_animator == null)
                return;
            if (!hasParameter(s_paramSkill))
                return;

            m_animator.SetTrigger(s_paramSkill);
        }

        // Animator 에 해당 hash 의 파라미터가 존재하는지. 없는 파라미터에 SetTrigger 하면 에러 로그가 나므로 선체크한다.
        private bool hasParameter(int paramHash)
        {
            foreach (AnimatorControllerParameter p in m_animator.parameters)
            {
                if (p.nameHash == paramHash)
                    return true;
            }
            return false;
        }
    }
}
