using TMPro;
using UnityEngine;
using UnityEngine.UI;

namespace Client.Game
{
    // Stage 공지 배너 HUD (서버 StageNoticeNtf 수신 시 표시).
    //
    // 코드 빌드 — 프리팹/아트 의존 없음. 화면 상단 중앙에 텍스트 한 줄을
    // durationMs 동안 표시한 뒤 사라진다. 스크립트 Notice() → 서버 StageNoticeNtf →
    // StageManager.onStageNoticeNtf → UI_StageNotice.Notify(message, durationMs) 경로로 들어온다.
    //
    // 공지는 게임 중에만 발생하므로 최초 호출 시 지연 생성(lazy)된다. 별도 배선/프리팹 불필요.
    public class UI_StageNotice : MonoBehaviour
    {
        private const float DefaultDurationSec = 3f;   // durationMs <= 0 일 때 기본 표시시간
        private const float FadeSec = 0.3f;            // 사라질 때 페이드 시간

        private static UI_StageNotice s_instance;

        private CanvasGroup m_group;
        private TextMeshProUGUI m_text;
        private float m_hideAtTime;   // 이 시각(Time.unscaledTime)이 지나면 페이드 시작

        // 외부 진입점. 인스턴스가 없으면 생성하고 배너를 띄운다.
        public static void Notify(string message, int durationMs)
        {
            // Unity 의 == null 은 파괴된 오브젝트도 true 라, 씬 전환으로 사라졌으면 재생성한다.
            if (s_instance == null)
                s_instance = create();

            s_instance.show(message, durationMs);
        }

        private static UI_StageNotice create()
        {
            // 기존 UI 와 계층을 맞추기 위해 @UI_Root 아래에 둔다 (없으면 만든다).
            GameObject root = GameObject.Find("@UI_Root");
            if (root == null)
                root = new GameObject { name = "@UI_Root" };

            GameObject go = new GameObject("UI_StageNotice");
            go.transform.SetParent(root.transform, worldPositionStays: false);

            UI_StageNotice notice = go.AddComponent<UI_StageNotice>();
            notice.build();
            return notice;
        }

        // 코드로 Canvas + 배경 패널 + 텍스트를 구성한다.
        private void build()
        {
            Canvas canvas = gameObject.AddComponent<Canvas>();
            canvas.renderMode = RenderMode.ScreenSpaceOverlay;
            canvas.overrideSorting = true;
            canvas.sortingOrder = 100;   // HUD/버프바/팝업 위에 표시

            CanvasScaler scaler = gameObject.AddComponent<CanvasScaler>();
            scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
            scaler.referenceResolution = new Vector2(1920f, 1080f);

            m_group = gameObject.AddComponent<CanvasGroup>();
            m_group.alpha = 0f;            // 숨김 상태로 시작
            m_group.interactable = false;
            m_group.blocksRaycasts = false;   // 입력을 가로채지 않음

            // 배경 패널 — 상단 중앙 앵커.
            GameObject panel = new GameObject("Panel", typeof(RectTransform), typeof(Image));
            panel.transform.SetParent(transform, worldPositionStays: false);
            RectTransform panelRt = panel.GetComponent<RectTransform>();
            panelRt.anchorMin = new Vector2(0.5f, 1f);
            panelRt.anchorMax = new Vector2(0.5f, 1f);
            panelRt.pivot = new Vector2(0.5f, 1f);
            panelRt.anchoredPosition = new Vector2(0f, -80f);   // 상단에서 약간 내려서
            panelRt.sizeDelta = new Vector2(900f, 64f);
            Image bg = panel.GetComponent<Image>();
            bg.color = new Color(0f, 0f, 0f, 0.55f);
            bg.raycastTarget = false;

            // 텍스트 — 패널을 가득 채우되 약간의 패딩.
            GameObject textGo = new GameObject("Text", typeof(RectTransform));
            textGo.transform.SetParent(panel.transform, worldPositionStays: false);
            RectTransform textRt = textGo.GetComponent<RectTransform>();
            textRt.anchorMin = Vector2.zero;
            textRt.anchorMax = Vector2.one;
            textRt.offsetMin = new Vector2(16f, 8f);
            textRt.offsetMax = new Vector2(-16f, -8f);

            m_text = textGo.AddComponent<TextMeshProUGUI>();
            if (TMP_Settings.defaultFontAsset != null)
                m_text.font = TMP_Settings.defaultFontAsset;
            m_text.alignment = TextAlignmentOptions.Center;
            m_text.enableAutoSizing = true;
            m_text.fontSizeMin = 18f;
            m_text.fontSizeMax = 36f;
            m_text.color = Color.white;
            m_text.raycastTarget = false;
        }

        private void show(string message, int durationMs)
        {
            m_text.text = message ?? string.Empty;

            float durationSec = (durationMs > 0) ? durationMs / 1000f : DefaultDurationSec;
            m_hideAtTime = Time.unscaledTime + durationSec;
            m_group.alpha = 1f;
        }

        private void Update()
        {
            if (m_group.alpha <= 0f)
                return;   // 이미 숨김.

            float remain = m_hideAtTime - Time.unscaledTime;
            if (remain <= 0f)
                m_group.alpha = 0f;
            else if (remain < FadeSec)
                m_group.alpha = remain / FadeSec;   // 마지막 FadeSec 동안 서서히 사라짐
        }
    }
}
