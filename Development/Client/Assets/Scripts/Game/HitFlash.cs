using System.Collections.Generic;
using UnityEngine;

namespace Client.Game
{
    // 피격 몬스터를 짧게 빨갛게 틴트한다. 순수 클라, 패킷 0.
    //
    // 왜 MaterialPropertyBlock 이 아니라 머티리얼 인스턴스인가:
    //   몬스터 아트는 커스텀(SRP Batcher) 셰이더라, MPB 로 _BaseColor 하나만 덮어쓰면 나머지
    //   per-material CBUFFER 값이 깨져 화면이 어둡게 렌더된다(MPB ↔ SRP Batcher 비호환).
    //   그래서 머티리얼을 몬스터당 1회 인스턴스화해 그 색을 직접 빨강 쪽으로 보간했다 복원한다.
    //   → 셰이더 CBUFFER 가 온전 → 어두워지는 버그 없음, 텍스처 보존.
    // 강도는 매 프레임 감쇠하고, 다시 맞으면 1로 리셋 → 연타에도 깜빡임 없이 빨갛게 유지된다.
    // 적중 1프레임에 hitstop/스케일팝/데미지텍스트와 동시 발화한다(동기화 인과 피드백).
    //
    // 비용: 피격된 몬스터는 머티리얼 인스턴스가 생겨 SRP 배칭에서 빠진다. 첫 피격 시 1회만 생성·재사용.
    //       물량 성능이 문제되면 MPB 호환 커스텀 플래시 셰이더로 전환(후속).
    public class HitFlash : MonoBehaviour
    {
        private const float k_fadeDuration = 0.12f;             // 강도 1 → 0 감쇠 시간
        private const float k_strength = 0.75f;                 // 최대 강도에서 빨강으로 가는 비율(0~1)
        private static readonly Color k_hitColor = new Color(1f, 0.2f, 0.15f);

        private static readonly int s_baseColorId = Shader.PropertyToID("_BaseColor");
        private static readonly int s_colorId = Shader.PropertyToID("_Color");

        private Material[] m_mats;     // 인스턴스 머티리얼(렌더러×슬롯 평탄화)
        private int[] m_prop;          // 머티리얼별 컬러 프로퍼티 id (0 = 컬러 없음/스킵)
        private Color[] m_orig;        // 머티리얼별 원본 색
        private float m_intensity;
        private bool m_active;
        private bool m_cached;

        // 대상 몬스터를 빨갛게 틴트한다. 겹치면 강도를 1로 리셋(연장)한다.
        public static void Trigger(MonsterObject monster)
        {
            if (monster == null)
                return;

            HitFlash hf = monster.GetComponent<HitFlash>();
            if (hf == null)
                hf = monster.gameObject.AddComponent<HitFlash>();

            hf.begin();
        }

        private void begin()
        {
            if (!cacheOnce())
                return;   // 틴트할 머티리얼/컬러 프로퍼티 없음

            m_intensity = 1f;   // 다시 맞으면 최대 강도로 리셋
            m_active = true;
        }

        // 렌더러 머티리얼을 인스턴스화해 1회 캐싱한다(몬스터당 1회). 틴트 가능한 머티리얼이 있으면 true.
        private bool cacheOnce()
        {
            if (m_cached)
                return m_mats.Length > 0;

            m_cached = true;
            Renderer[] rends = GetComponentsInChildren<Renderer>();
            List<Material> mats = new List<Material>();
            List<int> props = new List<int>();
            List<Color> origs = new List<Color>();

            foreach (Renderer r in rends)
            {
                Material[] inst = r.materials;   // 인스턴스 생성 + 이 렌더러에 적용
                foreach (Material m in inst)
                {
                    int prop = m.HasProperty(s_baseColorId) ? s_baseColorId
                             : m.HasProperty(s_colorId) ? s_colorId : 0;
                    mats.Add(m);
                    props.Add(prop);
                    origs.Add(prop != 0 ? m.GetColor(prop) : Color.white);
                }
            }

            m_mats = mats.ToArray();
            m_prop = props.ToArray();
            m_orig = origs.ToArray();
            return m_mats.Length > 0;
        }

        private void Update()
        {
            if (!m_active)
                return;

            m_intensity -= Time.unscaledDeltaTime / k_fadeDuration;
            bool done = m_intensity <= 0f;
            float k = done ? 0f : m_intensity;

            for (int i = 0; i < m_mats.Length; i++)
            {
                if (m_prop[i] == 0)
                    continue;

                Color c = Color.Lerp(m_orig[i], k_hitColor, k * k_strength);
                c.a = m_orig[i].a;   // 알파는 원본 유지
                m_mats[i].SetColor(m_prop[i], c);
            }

            if (done)
                m_active = false;   // 강도 0 → 원색으로 복원된 상태
        }
    }
}
