using UnityEngine;

namespace Client.Game
{
    // 피격 방향 표식. 본인(LocalPlayer)이 맞았을 때 발밑에 "공격자 쪽으로 뻗은 붉은 띠"(절차적)를
    // 잠깐 띄워 "어디서 맞았는지" 읽히게 한다. 몬스터가 많을 때 가독성용.
    //
    // ※ 아트 무의존 placeholder — MonsterTelegraph / DamageText 와 동일한 절차적 프리미티브 방식.
    //   나중에 데칼/스크린 방향표식 UI 로 교체 예정.
    public class HitDirectionIndicator : MonoBehaviour
    {
        private const float k_duration = 0.45f;   // 표시 시간(초)
        private const float k_groundY  = 0.05f;   // z-fighting 방지용 약간 띄움
        private const float k_length   = 2.2f;    // 플레이어→공격자 방향 길이
        private const float k_width    = 0.5f;
        private const float k_thickness = 0.05f;

        private float    m_life;
        private Renderer m_rend;
        private Transform m_bar;
        private static readonly Color k_color = new Color(1f, 0.25f, 0.15f, 1f);   // 위험 = 붉은색

        // playerPos 에서 attackerPos 방향으로 향한 표식을 띄운다.
        public static void Spawn(Vector3 playerPos, Vector3 attackerPos)
        {
            Vector3 dir = attackerPos - playerPos;
            dir.y = 0f;
            dir = (dir.sqrMagnitude > 0.0001f) ? dir.normalized : Vector3.forward;

            GameObject root = new GameObject("HitDirectionIndicator");
            // 플레이어 발밑에서 공격자 쪽으로 뻗도록 중심을 절반 길이만큼 민다.
            root.transform.position = new Vector3(playerPos.x, k_groundY, playerPos.z) + dir * (k_length * 0.5f);
            root.transform.rotation = Quaternion.LookRotation(dir);

            GameObject bar = GameObject.CreatePrimitive(PrimitiveType.Cube);   // local Z = forward = dir
            Collider col = bar.GetComponent<Collider>();
            if (col != null)
                Destroy(col);
            bar.transform.SetParent(root.transform, false);
            bar.transform.localScale = new Vector3(k_width, k_thickness, k_length);

            HitDirectionIndicator ind = root.AddComponent<HitDirectionIndicator>();
            ind.m_bar  = bar.transform;
            ind.m_rend = bar.GetComponent<Renderer>();
            ind.applyColor(k_color);

            Destroy(root, k_duration);
        }

        private void Update()
        {
            m_life += Time.deltaTime;
            float t = Mathf.Clamp01(m_life / k_duration);

            // 끝으로 갈수록 얇아지며 사라지는 느낌(폭 축소). URP Lit 은 알파가 안 먹어 스케일로 페이드 대체.
            if (m_bar != null)
            {
                float k = 1f - t;
                m_bar.localScale = new Vector3(k_width * k, k_thickness, k_length);
            }
        }

        private void applyColor(Color c)
        {
            if (m_rend == null)
                return;
            Material m = m_rend.material;   // 인스턴스 (오브젝트와 함께 파괴됨)
            if (m.HasProperty("_BaseColor")) m.SetColor("_BaseColor", c);
            if (m.HasProperty("_Color"))     m.SetColor("_Color", c);
        }
    }
}
