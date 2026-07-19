using TMPro;
using UnityEngine;
using UnityEngine.UI;

namespace Client.UI
{
    // 채팅 한 줄을 표시하는 SubItem UI.
    // 프리팹은 레이아웃 컨테이너만 제공하고, 텍스트는 여기서 생성해 프리팹 직렬화 의존을 줄인다.
    public class UI_ChatLine : UI_Base
    {
        private const float k_visibleDuration = 10f;
        private const float k_fadeDuration = 2f;

        private TextMeshProUGUI m_messageText;
        private CanvasGroup m_canvasGroup;
        private LayoutElement m_layoutElement;
        private float m_createdAt;
        private bool m_isExpanded;

        public override void Init()
        {
            m_messageText = GetComponentInChildren<TextMeshProUGUI>(includeInactive: true);
            if (m_messageText == null)
            {
                GameObject textObject = new GameObject("MessageText", typeof(RectTransform), typeof(TextMeshProUGUI));
                textObject.transform.SetParent(transform, worldPositionStays: false);

                RectTransform rect = textObject.GetComponent<RectTransform>();
                rect.anchorMin = Vector2.zero;
                rect.anchorMax = Vector2.one;
                rect.offsetMin = new Vector2(4f, 0f);
                rect.offsetMax = new Vector2(-4f, 0f);

                m_messageText = textObject.GetComponent<TextMeshProUGUI>();
                m_messageText.font = TMP_Settings.defaultFontAsset;
                m_messageText.fontSize = 16;
                m_messageText.color = Color.white;
                m_messageText.alignment = TextAlignmentOptions.TopLeft;
                m_messageText.richText = false;
                m_messageText.raycastTarget = false;
            }

            m_layoutElement = GetComponent<LayoutElement>();
            if (m_layoutElement == null)
                m_layoutElement = gameObject.AddComponent<LayoutElement>();
            m_layoutElement.minHeight = 22f;
            m_layoutElement.preferredHeight = 22f;

            m_canvasGroup = GetComponent<CanvasGroup>();
            if (m_canvasGroup == null)
                m_canvasGroup = gameObject.AddComponent<CanvasGroup>();
            m_canvasGroup.alpha = 1f;
            m_createdAt = Time.unscaledTime;
        }

        private void Update()
        {
            if (m_canvasGroup == null)
                return;

            if (m_isExpanded)
            {
                m_canvasGroup.alpha = 1f;
                return;
            }

            float elapsed = Time.unscaledTime - m_createdAt;
            if (elapsed <= k_visibleDuration)
                return;

            float fadeProgress = (elapsed - k_visibleDuration) / k_fadeDuration;
            m_canvasGroup.alpha = 1f - Mathf.Clamp01(fadeProgress);
        }

        public void SetMessage(string message, Color color)
        {
            if (m_messageText == null)
                return;

            m_messageText.text = message;
            m_messageText.color = color;

            float preferredHeight = m_messageText.GetPreferredValues(message, 488f, 0f).y + 2f;
            m_layoutElement.preferredHeight = Mathf.Clamp(preferredHeight, 22f, 44f);
        }

        public void SetExpanded(bool expanded)
        {
            m_isExpanded = expanded;
            if (m_canvasGroup == null)
                return;

            m_canvasGroup.alpha = 1f;
            if (!expanded)
                m_createdAt = Time.unscaledTime;
        }
    }
}
