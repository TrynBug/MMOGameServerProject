using UnityEngine;

namespace Client.Game
{
    // 피격 시 메시를 짧게 부풀렸다 복원한다(squash & stretch). 순수 클라, 패킷 0.
    //
    // 80ms 동안 1.0 → 1.12 → 1.0 으로 튕겨 "맞았다"를 눈에 등록시킨다. 싸고 효과 큰 juice.
    // 적중 1프레임에 hitstop/데미지텍스트와 동시 발화한다(동기화 인과 피드백).
    //
    // 헬스바 등 루트 부착물까지 같이 커지지 않도록 아트(Animator) 트랜스폼만 스케일한다.
    public class ScalePop : MonoBehaviour
    {
        private const float k_duration = 0.0f;   // 비활성화 함. 기존 값은 0.08f(80ms) 였음.
        private const float k_amount = 0.0f;     // 비활성화 함. 기존 값은 0.15f(+15%) 였음.

        private Transform m_target;
        private Vector3 m_baseScale;
        private float m_timer;
        private bool m_active;

        // 대상 몬스터에 스케일 팝을 적용한다. 이미 진행 중이면 처음부터 다시 시작(연장 아님).
        public static void Play(MonsterObject monster)
        {
            if (monster == null)
                return;

            ScalePop sp = monster.GetComponent<ScalePop>();
            if (sp == null)
                sp = monster.gameObject.AddComponent<ScalePop>();

            sp.begin();
        }

        private void begin()
        {
            if (!m_active)
            {
                // 아트 메시 트랜스폼만 팝(루트의 헬스바 등은 제외). 없으면 루트로 폴백.
                Animator anim = GetComponentInChildren<Animator>();
                m_target = anim != null ? anim.transform : transform;
                m_baseScale = m_target.localScale;

                m_active = true;
            }

            m_timer = 0f;   // 재타격 시 처음부터
        }

        private void Update()
        {
            if (!m_active)
                return;

            m_timer += Time.unscaledDeltaTime;
            float p = m_timer / k_duration;

            if (p >= 1f)
            {
                m_target.localScale = m_baseScale;
                m_active = false;
                return;
            }

            float s = 1f + k_amount * Mathf.Sin(p * Mathf.PI);   // 0→1→0 으로 부드럽게 부풀었다 복원
            m_target.localScale = m_baseScale * s;
        }

        // 디스폰 등으로 비활성화될 때 스케일 복원.
        private void OnDisable()
        {
            if (!m_active)
                return;

            if (m_target != null)
                m_target.localScale = m_baseScale;

            m_active = false;
        }
    }
}
