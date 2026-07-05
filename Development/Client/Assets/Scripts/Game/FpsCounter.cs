using TMPro;
using UnityEngine;
using UnityEngine.UI;

namespace Client.Game
{
    // 화면 왼쪽 위에 현재 FPS 를 표시하는 경량 오버레이. 클라 전용, 패킷 0.
    //
    // GameBootstrap 이 1회 생성하고 DontDestroyOnLoad 로 유지 → 모든 씬 위에 항상 표시된다.
    // 자체 Overlay Canvas(최상단 sortingOrder)를 만들어 다른 UI 위에 그린다.
    //
    // FPS 는 프레임마다 지수 평활(EMA)로 안정화하고, 텍스트는 0.25초마다 갱신해 숫자가 덜 튄다.
    // 색상: 55↑ 초록 / 30↑ 노랑 / 그 이하 빨강.
    public class FpsCounter : MonoBehaviour
    {
        private const float k_updateInterval = 0.25f;   // 텍스트 갱신 주기(초)
        private const int   k_sortingOrder   = 32000;   // 항상 최상단
        private const float k_smoothing      = 0.1f;    // EMA 계수(클수록 즉각적)

        private TextMeshProUGUI m_text;
        private float m_smoothedFps = 60f;
        private float m_timer;

        // GameBootstrap 에서 호출. 이미 있으면 중복 생성하지 않는다.
        public static void Create()
        {
            if (FindAnyObjectByType<FpsCounter>() != null) return;
            var go = new GameObject("[FpsCounter]");
            DontDestroyOnLoad(go);
            go.AddComponent<FpsCounter>();
        }

        private void Awake()
        {
            // 전용 Overlay Canvas (게임 UI 와 독립, 항상 위).
            var canvas = gameObject.AddComponent<Canvas>();
            canvas.renderMode = RenderMode.ScreenSpaceOverlay;
            canvas.sortingOrder = k_sortingOrder;

            var scaler = gameObject.AddComponent<CanvasScaler>();
            scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
            scaler.referenceResolution = new Vector2(1920, 1080);
            scaler.matchWidthOrHeight = 0.5f;

            // 가독성용 반투명 배경 박스.
            var bgRT = newChild("Bg");
            bgRT.anchorMin = bgRT.anchorMax = bgRT.pivot = new Vector2(0, 1);
            bgRT.anchoredPosition = new Vector2(10, -10);
            bgRT.sizeDelta = new Vector2(128, 40);
            var bg = bgRT.gameObject.AddComponent<Image>();
            bg.color = new Color(0f, 0f, 0f, 0.45f);
            bg.raycastTarget = false;

            // FPS 텍스트.
            var txtRT = newChild("Text");
            txtRT.SetParent(bgRT, false);
            txtRT.anchorMin = new Vector2(0, 0);
            txtRT.anchorMax = new Vector2(1, 1);
            txtRT.offsetMin = new Vector2(12, 0);
            txtRT.offsetMax = new Vector2(-8, 0);
            m_text = txtRT.gameObject.AddComponent<TextMeshProUGUI>();
            m_text.fontSize = 24;
            m_text.alignment = TextAlignmentOptions.Left;
            m_text.raycastTarget = false;
            m_text.text = "FPS --";
        }

        private RectTransform newChild(string name)
        {
            var go = new GameObject(name, typeof(RectTransform));
            var rt = go.GetComponent<RectTransform>();
            rt.SetParent(transform, false);
            return rt;
        }

        private void Update()
        {
            float dt = Time.unscaledDeltaTime;
            if (dt > 0f)
                m_smoothedFps = Mathf.Lerp(m_smoothedFps, 1f / dt, k_smoothing);

            m_timer += dt;
            if (m_timer < k_updateInterval) return;
            m_timer = 0f;

            int fps = Mathf.RoundToInt(m_smoothedFps);
            m_text.text = $"FPS {fps}";
            m_text.color = fps >= 55 ? new Color(0.55f, 1f, 0.6f)
                         : fps >= 30 ? new Color(1f, 0.85f, 0.4f)
                                     : new Color(1f, 0.45f, 0.45f);
        }
    }
}
