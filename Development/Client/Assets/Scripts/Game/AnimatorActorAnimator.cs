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
    }
}
