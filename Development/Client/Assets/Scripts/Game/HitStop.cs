using UnityEngine;

namespace Client.Game
{
    // 피격 몬스터의 애니메이션을 짧게 멈춘다(hitstop). 순수 클라, 패킷 0.
    //
    // 타격 순간 애니를 몇 프레임 얼렸다 풀어 "한 방이 묵직하다"는 인상을 준다.
    // 위치는 서버 권위라 건드리지 않는다 — 위치까지 홀드하면 원격 보간 시각(RenderTime)이
    // 정지 중에도 계속 흘러, 해제 순간 현재 위치로 한 프레임에 튀어(순간이동) 보인다.
    // 그래서 위치는 SnapshotInterpolator 가 계속 매끄럽게 굴리고, 애니만 멈춘다.
    // (정지 중 미끄러지는 거리는 33~50ms 분량이라 사실상 안 보인다.)
    // 가해자(플레이어 본인)는 절대 얼리지 않는다(즉발 조작감). 피격 대상(몬스터) 전용.
    public class HitStop : MonoBehaviour
    {
        private Animator m_animator;
        private float m_prevAnimSpeed;
        private float m_timer;
        private bool m_active;

        // 대상 몬스터에 hitstop 을 적용한다. durationMs 동안 애니 정지. 겹치면 더 긴 쪽으로 연장.
        public static void Trigger(MonsterObject monster, float durationMs)
        {
            if (monster == null)
                return;

            HitStop hs = monster.GetComponent<HitStop>();
            if (hs == null)
                hs = monster.gameObject.AddComponent<HitStop>();

            hs.begin(durationMs);
        }

        private void begin(float durationMs)
        {
            if (!m_active)
            {
                m_animator = GetComponentInChildren<Animator>();
                m_active = true;
                if (m_animator != null)
                {
                    m_prevAnimSpeed = m_animator.speed;
                    m_animator.speed = 0f;
                }
            }

            m_timer = Mathf.Max(m_timer, durationMs / 1000f);   // 겹치면 더 긴 쪽
        }

        private void Update()
        {
            if (!m_active)
                return;

            m_timer -= Time.unscaledDeltaTime;   // timeScale 의 영향을 받지 않게(글로벌 정지 금지 원칙)
            if (m_timer <= 0f)
                end();
        }

        private void end()
        {
            if (m_animator != null)
                m_animator.speed = m_prevAnimSpeed;
            m_active = false;
        }

        // 디스폰 등으로 비활성화될 때 애니 속도를 복원한다(풀링 재사용 대비).
        private void OnDisable()
        {
            if (!m_active)
                return;
            if (m_animator != null)
                m_animator.speed = m_prevAnimSpeed;
            m_active = false;
        }
    }
}
