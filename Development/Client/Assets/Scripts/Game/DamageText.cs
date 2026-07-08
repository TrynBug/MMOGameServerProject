using UnityEngine;

namespace Client.Game
{
    // 코드로 생성하는 데미지 숫자 placeholder. 프리팹/아트 의존 없음.
    // 대상 머리 위에서 잠깐 떠오르며 페이드 아웃한다. (나중에 월드스페이스 UI + 풀링으로 교체)
    public class DamageText : MonoBehaviour
    {
        private const float k_duration = 0.8f;
        private const float k_riseSpeed = 1.5f;

        private float m_life;
        private TextMesh m_text;
        private Camera m_cam;

        // worldPos 에 데미지 숫자를 띄운다. isDuplicate 면 회색(감소 대미지), 아니면 노랑.
        public static void Spawn(Vector3 worldPos, float damage, bool isDuplicate)
        {
            GameObject go = new GameObject("DamageText");
            go.transform.position = worldPos;
            go.AddComponent<DamageText>().init(damage, isDuplicate);
        }

        private void init(float damage, bool isDuplicate)
        {
            m_cam = Camera.main;

            m_text = gameObject.AddComponent<TextMesh>();
            m_text.text = Mathf.RoundToInt(damage).ToString();
            m_text.fontSize = 80;
            m_text.characterSize = 0.05f;
            m_text.anchor = TextAnchor.MiddleCenter;
            m_text.alignment = TextAlignment.Center;
            m_text.color = isDuplicate ? new Color(0.7f, 0.7f, 0.7f) : Color.red;

            // TextMesh 는 폰트가 없으면 렌더되지 않으므로 빌트인 폰트를 명시적으로 지정한다.
            Font font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
            if (font == null)
                font = Resources.GetBuiltinResource<Font>("Arial.ttf");   // 구버전 폴백
            if (font != null)
            {
                m_text.font = font;
                MeshRenderer mr = GetComponent<MeshRenderer>();
                if (mr != null)
                    mr.sharedMaterial = font.material;
            }
        }

        private void Update()
        {
            m_life += Time.deltaTime;
            transform.position += Vector3.up * (k_riseSpeed * Time.deltaTime);

            // 카메라를 바라보게 (빌보드).
            if (m_cam != null)
                transform.rotation = m_cam.transform.rotation;

            // 페이드 아웃.
            if (m_text != null)
            {
                Color c = m_text.color;
                c.a = Mathf.Clamp01(1f - m_life / k_duration);
                m_text.color = c;
            }

            if (m_life >= k_duration)
                Destroy(gameObject);
        }
    }
}
