using UnityEngine;

namespace Client.Game
{
    // 액터(캐릭터/몬스터) 머리 위에 떠 있는 체력바 placeholder.
    // DamageText 와 같은 사상: 프리팹/아트 의존 없이 코드로 quad(배경+채움) + TextMesh 를 만들어
    // 카메라를 향해 빌보드로 띄운다. (나중에 월드스페이스 UI 프리팹 + 풀링으로 교체)
    //
    // 동작:
    //   - 대상 ActorObject 의 CurHp/MaxHp 를 매 프레임 폴링해 채움 비율 + 숫자를 갱신한다(이벤트 구독 없음).
    //   - 대상 머리 위(모델 바운드 상단)를 따라다닌다(부모로 붙이지 않음 → 대상 스케일/회전의 영향을 받지 않음).
    //   - 몬스터 몸통에 가려지지 않도록 깊이 테스트를 끄고 항상 위에 그린다(MMORPG 머리위 체력바 방식).
    //   - 최대HP 가 아직 0(서버 스탯 미수신)이거나 사망 상태면 숨긴다.
    //   - 대상이 파괴되면(디스폰) 스스로 정리된다.
    public class HealthBar : MonoBehaviour
    {
        // placeholder 상수 (월드 유닛). 아트 확정 후 데이터/프리팹으로 이전.
        private const float k_defaultHeightOffset = 2.0f;   // 모델 바운드를 못 구할 때의 폴백 높이
        private const float k_headGap = 0.3f;               // 모델 머리 위로 띄울 여유 간격
        private const float k_width = 0.8f;
        private const float k_height = 0.1f;
        private const float k_border = 0.015f;              // 배경이 만드는 테두리 두께

        // 안쪽(채움 영역) 크기. 테두리를 뺀 값.
        private const float k_innerWidth = k_width - k_border * 2f;
        private const float k_innerHeight = k_height - k_border * 2f;

        // 그리는 순서(렌더 큐). 큐가 클수록 나중에(위에) 그려진다: 배경 < 채움 < 글자.
        // Overlay(4000) 대역이라 월드의 투명 오브젝트들보다 뒤에 그려진다.
        private const int k_queueBg = 4000;
        private const int k_queueFill = 4001;
        private const int k_queueText = 4002;

        private ActorObject m_target;
        private float m_heightOffset;
        private Camera m_cam;

        private MeshRenderer m_bgRenderer;
        private MeshRenderer m_fillRenderer;
        private MeshRenderer m_textRenderer;
        private Transform m_fillTr;        // 채움 quad. x 스케일/위치로 비율을 표현.
        private Material m_fillMat;         // 색상 갱신용 (per-instance).
        private TextMesh m_text;            // 현재/최대 HP 숫자 (개발용).
        private bool m_visible = true;

        // target 머리 위에 체력바를 생성한다. (DamageText.Spawn 과 같은 진입점)
        // heightOffset 을 음수로 두면(기본) 모델 바운드 상단에서 높이를 자동 계산한다.
        public static HealthBar Attach(ActorObject target, float heightOffset = -1f)
        {
            if (target == null)
                return null;

            GameObject go = new GameObject($"HealthBar_{target.name}");
            HealthBar bar = go.AddComponent<HealthBar>();
            bar.init(target, heightOffset);
            return bar;
        }

        private void init(ActorObject target, float heightOffset)
        {
            m_target = target;
            m_heightOffset = (heightOffset >= 0f) ? heightOffset : computeHeadOffset(target);
            m_cam = Camera.main;

            // 배경 quad (어두운 바탕 = 테두리). holder(this) 의 원점.
            GameObject bgGo = buildQuad("BG", new Color(0f, 0f, 0f, 0.6f), k_queueBg, transform);
            bgGo.transform.localScale = new Vector3(k_width, k_height, 1f);
            bgGo.transform.localPosition = Vector3.zero;
            m_bgRenderer = bgGo.GetComponent<MeshRenderer>();

            // 채움 quad. 크기/위치는 Update 에서 갱신.
            GameObject fillGo = buildQuad("Fill", Color.green, k_queueFill, transform);
            m_fillRenderer = fillGo.GetComponent<MeshRenderer>();
            m_fillTr = fillGo.transform;
            m_fillMat = m_fillRenderer.sharedMaterial;

            buildText();
        }

        // 모델의 모든 Renderer 바운드를 합쳐 머리(상단) 높이를 구한다. (큰 몬스터/작은 몬스터 자동 대응)
        // 체력바 자신의 quad 는 별도 GameObject 라 여기 포함되지 않는다.
        private static float computeHeadOffset(ActorObject target)
        {
            Renderer[] rends = target.GetComponentsInChildren<Renderer>();
            if (rends == null || rends.Length == 0)
                return k_defaultHeightOffset;

            Bounds b = rends[0].bounds;
            for (int i = 1; i < rends.Length; i++)
                b.Encapsulate(rends[i].bounds);

            float top = b.max.y - target.transform.position.y;
            return top + k_headGap;
        }

        // 단색 unlit quad 를 만든다. 콜라이더 제거 + 그림자 off + 항상 위에 그림.
        private static GameObject buildQuad(string name, Color color, int renderQueue, Transform parent)
        {
            GameObject go = GameObject.CreatePrimitive(PrimitiveType.Quad);
            go.name = name;

            // Quad 기본 MeshCollider 제거 (지면 클릭/투사체 히트 방해 방지).
            Collider col = go.GetComponent<Collider>();
            if (col != null)
                Destroy(col);

            MeshRenderer mr = go.GetComponent<MeshRenderer>();
            mr.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
            mr.receiveShadows = false;

            // UI/Default: 렌더파이프라인 무관 + 단색(틴트) + 양면 + unlit + ZTest 제어 가능.
            Material mat = new Material(Shader.Find("UI/Default"));
            mat.color = color;
            // 깊이 테스트를 Always 로 → 몬스터 몸통에 가려지지 않고 항상 위에 그려진다.
            mat.SetInt("unity_GUIZTestMode", (int)UnityEngine.Rendering.CompareFunction.Always);
            mat.renderQueue = renderQueue;
            mr.sharedMaterial = mat;

            go.transform.SetParent(parent, false);
            return go;
        }

        // 현재/최대 HP 를 보여줄 TextMesh 를 만든다. (DamageText 와 동일하게 빌트인 폰트 사용)
        private void buildText()
        {
            GameObject txtGo = new GameObject("HpText");
            txtGo.transform.SetParent(transform, false);
            txtGo.transform.localPosition = new Vector3(0f, 0f, -0.002f);

            m_text = txtGo.AddComponent<TextMesh>();
            m_text.fontSize = 64;
            m_text.characterSize = 0.012f;   // 0.1 높이 바에 맞춘 placeholder 크기. 안 맞으면 이 값만 조정.
            m_text.anchor = TextAnchor.MiddleCenter;
            m_text.alignment = TextAlignment.Center;
            m_text.color = Color.white;

            m_textRenderer = txtGo.GetComponent<MeshRenderer>();

            Font font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
            if (font == null)
                font = Resources.GetBuiltinResource<Font>("Arial.ttf");   // 구버전 폴백
            if (font != null)
            {
                m_text.font = font;
                // 폰트 머터리얼 인스턴스 → 채움보다 나중에(위에) 그려 글자가 가려지지 않게.
                Material textMat = new Material(font.material);
                textMat.renderQueue = k_queueText;
                m_textRenderer.sharedMaterial = textMat;
            }
        }

        private void Update()
        {
            // 대상이 디스폰되어 파괴됨 → 스스로 정리.
            if (m_target == null)
            {
                Destroy(gameObject);
                return;
            }

            double maxHp = m_target.MaxHp;
            bool visible = !m_target.IsDead && maxHp > 0.0;
            if (visible != m_visible)
                setVisible(visible);

            if (!visible)
                return;

            // 머리 위 추종.
            transform.position = m_target.transform.position + Vector3.up * m_heightOffset;

            // 카메라 빌보드 (DamageText 와 동일).
            if (m_cam == null)
                m_cam = Camera.main;
            if (m_cam != null)
                transform.rotation = m_cam.transform.rotation;

            // 채움 비율 + 좌측 정렬 (왼쪽 끝 고정, 오른쪽으로 줄어듦).
            float ratio = Mathf.Clamp01((float)(m_target.CurHp / maxHp));
            float w = k_innerWidth * ratio;
            m_fillTr.localScale = new Vector3(w, k_innerHeight, 1f);
            m_fillTr.localPosition = new Vector3(-k_innerWidth * 0.5f + w * 0.5f, 0f, -0.001f);

            // 색: 체력 높으면 초록 → 낮으면 빨강.
            m_fillMat.color = Color.Lerp(Color.red, Color.green, ratio);

            // 숫자 갱신: 현재/최대 (개발용).
            m_text.text = Mathf.RoundToInt((float)m_target.CurHp) + "/" + Mathf.RoundToInt((float)maxHp);
        }

        private void setVisible(bool visible)
        {
            m_visible = visible;
            m_bgRenderer.enabled = visible;
            m_fillRenderer.enabled = visible;
            if (m_textRenderer != null)
                m_textRenderer.enabled = visible;
        }

        private void OnDestroy()
        {
            // 코드로 만든 머터리얼 인스턴스 정리 (누수 방지).
            if (m_bgRenderer != null && m_bgRenderer.sharedMaterial != null)
                Destroy(m_bgRenderer.sharedMaterial);
            if (m_fillMat != null)
                Destroy(m_fillMat);
            if (m_textRenderer != null && m_textRenderer.sharedMaterial != null)
                Destroy(m_textRenderer.sharedMaterial);
        }
    }
}
