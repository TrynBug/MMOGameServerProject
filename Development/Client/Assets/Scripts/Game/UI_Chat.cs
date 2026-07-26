using System;
using System.Collections.Generic;
using System.Text;
using Client.Managers;
using Client.Network;
using Client.Packet;
using Client.UI;
using Common;
using GameData;
using GamePacket;
using TMPro;
using UnityEngine;
using UnityEngine.InputSystem;
using UnityEngine.UI;

namespace Client.Game
{
    // 게임 씬에서 항상 표시되는 채팅 HUD.
    // ChatRecvNtf를 받아 최근 메시지를 표시하고, 채널/서버 범위로 ChatSendReq를 전송한다.
    // 프리팹 경로: Resources/UI/Scene/UI_Chat
    public class UI_Chat : UI_Scene
    {
        private enum Objects { MessageContent }
        private enum Texts { ChannelText }

        private const int k_maxLineCount = 50;

        private static readonly Color s_panelIdleColor = new Color(0.025f, 0.035f, 0.055f, 0f);
        private static readonly Color s_panelEditingColor = new Color(0.025f, 0.035f, 0.055f, 0.88f);

        private Transform m_messageContent;
        private readonly Queue<UI_ChatLine> m_lines = new Queue<UI_ChatLine>();
        private TMP_InputField m_inputField;
        private GameObject m_inputObject;
        private ScrollRect m_scrollRect;
        private Image m_viewportRaycastTarget;
        private GameObject m_scrollbarObject;
        private Image m_panelBackground;
        private Image m_panelAccent;
        private Image m_channelBackground;
        private readonly Image[] m_channelChevron = new Image[2];
        private TextMeshProUGUI m_channelText;
        private TextMeshProUGUI m_placeholderText;
        private ChatType m_chatType = ChatType.StageChannel;
        private bool m_isEditing;
        private bool m_scrollToLatestPending;

        public override void Init()
        {
            base.Init();

            Bind<GameObject>(typeof(Objects));
            Bind<TextMeshProUGUI>(typeof(Texts));

            GameObject messageContent = Get<GameObject>((int)Objects.MessageContent);
            m_messageContent = messageContent != null ? messageContent.transform : transform;

            m_channelText = Get<TextMeshProUGUI>((int)Texts.ChannelText);
            createPanelBackground();
            styleMessageContent();
            styleChannelButton();
            createInputField();
            updateChatTypeVisual();
            updateEditingVisual();

            PacketDispatcher.Instance.Register<ChatRecvNtf>(GamePacketId.ChatRecvNtf, onChatRecvNtf);
            PacketDispatcher.Instance.Register<ChatSendRes>(GamePacketId.ChatSendRes, onChatSendRes);
        }

        private void Update()
        {
            Keyboard keyboard = Keyboard.current;
            if (keyboard == null)
                return;

            if (!m_isEditing)
            {
                if (keyboard.enterKey.wasPressedThisFrame)
                    openInput();
                return;
            }

            if (keyboard.escapeKey.wasPressedThisFrame)
            {
                closeInput();
                return;
            }

            // 포커스가 있을 때의 Enter 전송은 TMP_InputField.onSubmit에서 처리한다.
            // 포커스가 없다면 Enter는 채팅창을 닫지 않고 입력 칸에 다시 포커스를 준다.
            if (keyboard.enterKey.wasPressedThisFrame && m_inputField != null && !m_inputField.isFocused)
                focusInput();
        }

        private void LateUpdate()
        {
            if (!m_scrollToLatestPending || m_scrollRect == null)
                return;

            Canvas.ForceUpdateCanvases();
            if (m_messageContent is RectTransform contentRect)
                LayoutRebuilder.ForceRebuildLayoutImmediate(contentRect);

            m_scrollRect.verticalNormalizedPosition = 0f;
            m_scrollToLatestPending = false;
        }

        private void OnDestroy()
        {
            // 게임 씬을 나간 뒤에는 파괴된 HUD가 패킷을 처리하지 않도록 해제한다.
            PacketDispatcher.Instance.Unregister(GamePacketId.ChatRecvNtf);
            PacketDispatcher.Instance.Unregister(GamePacketId.ChatSendRes);

            Managers.Managers.Input.SetKeyboardGameplayBlocked(false);
            Managers.Managers.Input.SetMenuInputBlocked(false);
        }

        private void onChatRecvNtf(ChatRecvNtf ntf)
        {
            addMessage($"[{getChatTypeName(ntf.ChatType)}] {ntf.SenderName}: {ntf.Message}", getChatTypeColor(ntf.ChatType));
        }

        private void onChatSendRes(ChatSendRes res)
        {
            if ((EResultCode)res.ResultCode != EResultCode.Success)
            {
                addSystemMessage(string.IsNullOrEmpty(res.ErrorMsg) ? "채팅 전송에 실패했습니다." : res.ErrorMsg);
                return;
            }

            if (res.ChatType == ChatType.Whisper)
                addMessage($"[귓속말 → {res.TargetName}] {res.SenderName}: {res.Message}", getChatTypeColor(ChatType.Whisper));
        }

        private void addSystemMessage(string message)
        {
            addMessage($"[시스템] {message}", new Color(1f, 0.66f, 0.34f));
        }

        private void addMessage(string message, Color color)
        {
            bool shouldFollowLatest = !m_isEditing || isViewingLatest();
            UI_ChatLine line = Managers.Managers.UI.MakeSubItem<UI_ChatLine>(m_messageContent);
            if (line == null)
                return;

            line.Init();
            line.SetMessage($"[{DateTime.Now:HH:mm}] {message}", color);
            line.SetExpanded(m_isEditing);
            m_lines.Enqueue(line);

            if (m_lines.Count > k_maxLineCount)
            {
                UI_ChatLine oldest = m_lines.Dequeue();
                if (oldest != null)
                    Managers.Managers.Resource.Destroy(oldest.gameObject);
            }

            if (shouldFollowLatest)
                m_scrollToLatestPending = true;
        }

        private bool isViewingLatest()
        {
            if (m_scrollRect == null || m_scrollRect.content == null || m_scrollRect.viewport == null)
                return true;

            if (m_scrollRect.content.rect.height <= m_scrollRect.viewport.rect.height + 1f)
                return true;

            return m_scrollRect.verticalNormalizedPosition <= 0.02f;
        }

        private static string getChatTypeName(ChatType chatType)
        {
            switch (chatType)
            {
                case ChatType.StageChannel: return "채널";
                case ChatType.GameServer: return "서버";
                case ChatType.Global: return "전체";
                case ChatType.Whisper: return "귓속말";
                default: return "채팅";
            }
        }

        private static Color getChatTypeColor(ChatType chatType)
        {
            switch (chatType)
            {
                case ChatType.StageChannel: return new Color(0.88f, 0.92f, 1f);
                case ChatType.GameServer:   return new Color(0.42f, 0.82f, 1f);
                case ChatType.Global:       return new Color(1f, 0.78f, 0.30f);
                case ChatType.Whisper:      return new Color(0.95f, 0.56f, 1f);
                default:                    return Color.white;
            }
        }

        private void createPanelBackground()
        {
            GameObject panelObject = new GameObject("ChatPanel", typeof(RectTransform), typeof(Image));
            panelObject.transform.SetParent(transform, worldPositionStays: false);
            panelObject.transform.SetAsFirstSibling();

            RectTransform rect = panelObject.GetComponent<RectTransform>();
            rect.anchorMin = Vector2.zero;
            rect.anchorMax = Vector2.zero;
            rect.anchoredPosition = new Vector2(10f, 10f);
            rect.sizeDelta = new Vector2(520f, 228f);
            rect.pivot = Vector2.zero;

            m_panelBackground = panelObject.GetComponent<Image>();
            m_panelBackground.color = s_panelIdleColor;
            m_panelBackground.raycastTarget = false;

            GameObject accentObject = new GameObject("TopAccent", typeof(RectTransform), typeof(Image));
            accentObject.transform.SetParent(panelObject.transform, worldPositionStays: false);

            RectTransform accentRect = accentObject.GetComponent<RectTransform>();
            accentRect.anchorMin = new Vector2(0f, 1f);
            accentRect.anchorMax = Vector2.one;
            accentRect.anchoredPosition = Vector2.zero;
            accentRect.sizeDelta = new Vector2(0f, 2f);
            accentRect.pivot = new Vector2(0.5f, 1f);

            m_panelAccent = accentObject.GetComponent<Image>();
            m_panelAccent.color = new Color(0.25f, 0.68f, 1f, 0.9f);
            m_panelAccent.raycastTarget = false;
        }

        private void styleMessageContent()
        {
            if (m_messageContent == null || m_messageContent == transform)
                return;

            GameObject viewportObject = new GameObject(
                "ChatViewport",
                typeof(RectTransform),
                typeof(Image),
                typeof(RectMask2D),
                typeof(ScrollRect));
            viewportObject.transform.SetParent(transform, worldPositionStays: false);

            RectTransform viewportRect = viewportObject.GetComponent<RectTransform>();
            viewportRect.anchorMin = Vector2.zero;
            viewportRect.anchorMax = Vector2.zero;
            viewportRect.anchoredPosition = new Vector2(18f, 58f);
            viewportRect.sizeDelta = new Vector2(504f, 176f);
            viewportRect.pivot = Vector2.zero;

            m_viewportRaycastTarget = viewportObject.GetComponent<Image>();
            m_viewportRaycastTarget.color = Color.clear;
            m_viewportRaycastTarget.raycastTarget = false;

            m_messageContent.SetParent(viewportObject.transform, worldPositionStays: false);
            RectTransform contentRect = m_messageContent as RectTransform;
            contentRect.anchorMin = Vector2.zero;
            contentRect.anchorMax = new Vector2(1f, 0f);
            contentRect.anchoredPosition = Vector2.zero;
            contentRect.sizeDelta = Vector2.zero;
            contentRect.pivot = new Vector2(0.5f, 0f);

            VerticalLayoutGroup layout = m_messageContent.GetComponent<VerticalLayoutGroup>();
            if (layout != null)
            {
                layout.padding = new RectOffset(4, 8, 4, 4);
                layout.spacing = 2f;
                layout.childAlignment = TextAnchor.UpperLeft;
            }

            ContentSizeFitter sizeFitter = m_messageContent.GetComponent<ContentSizeFitter>();
            if (sizeFitter == null)
                sizeFitter = m_messageContent.gameObject.AddComponent<ContentSizeFitter>();
            sizeFitter.horizontalFit = ContentSizeFitter.FitMode.Unconstrained;
            sizeFitter.verticalFit = ContentSizeFitter.FitMode.PreferredSize;

            m_scrollRect = viewportObject.GetComponent<ScrollRect>();
            m_scrollRect.content = contentRect;
            m_scrollRect.viewport = viewportRect;
            m_scrollRect.horizontal = false;
            m_scrollRect.vertical = true;
            m_scrollRect.movementType = ScrollRect.MovementType.Clamped;
            m_scrollRect.inertia = true;
            m_scrollRect.decelerationRate = 0.135f;
            m_scrollRect.scrollSensitivity = 24f;

            createScrollbar(viewportObject.transform);
            m_scrollToLatestPending = true;
        }

        private void createScrollbar(Transform parent)
        {
            m_scrollbarObject = new GameObject("ChatScrollbar", typeof(RectTransform), typeof(Image), typeof(Scrollbar));
            m_scrollbarObject.transform.SetParent(parent, worldPositionStays: false);

            RectTransform scrollbarRect = m_scrollbarObject.GetComponent<RectTransform>();
            scrollbarRect.anchorMin = new Vector2(1f, 0f);
            scrollbarRect.anchorMax = Vector2.one;
            scrollbarRect.anchoredPosition = new Vector2(-2f, 0f);
            scrollbarRect.sizeDelta = new Vector2(4f, -8f);
            scrollbarRect.pivot = new Vector2(1f, 0.5f);

            Image background = m_scrollbarObject.GetComponent<Image>();
            background.color = new Color(0.2f, 0.3f, 0.42f, 0.25f);

            GameObject slidingAreaObject = new GameObject("SlidingArea", typeof(RectTransform));
            slidingAreaObject.transform.SetParent(m_scrollbarObject.transform, worldPositionStays: false);
            RectTransform slidingAreaRect = slidingAreaObject.GetComponent<RectTransform>();
            slidingAreaRect.anchorMin = Vector2.zero;
            slidingAreaRect.anchorMax = Vector2.one;
            slidingAreaRect.sizeDelta = Vector2.zero;

            GameObject handleObject = new GameObject("Handle", typeof(RectTransform), typeof(Image));
            handleObject.transform.SetParent(slidingAreaObject.transform, worldPositionStays: false);
            RectTransform handleRect = handleObject.GetComponent<RectTransform>();
            handleRect.anchorMin = Vector2.zero;
            handleRect.anchorMax = Vector2.one;
            handleRect.sizeDelta = Vector2.zero;

            Image handle = handleObject.GetComponent<Image>();
            handle.color = new Color(0.35f, 0.72f, 1f, 0.8f);

            Scrollbar scrollbar = m_scrollbarObject.GetComponent<Scrollbar>();
            scrollbar.handleRect = handleRect;
            scrollbar.targetGraphic = handle;
            scrollbar.direction = Scrollbar.Direction.BottomToTop;
            scrollbar.value = 0f;

            m_scrollRect.verticalScrollbar = scrollbar;
            m_scrollRect.verticalScrollbarVisibility = ScrollRect.ScrollbarVisibility.AutoHide;
            m_scrollbarObject.SetActive(false);
        }

        private void styleChannelButton()
        {
            if (m_channelText == null)
                return;

            GameObject buttonObject = new GameObject("ChannelButton", typeof(RectTransform), typeof(Image));
            buttonObject.transform.SetParent(transform, worldPositionStays: false);

            RectTransform rect = buttonObject.GetComponent<RectTransform>();
            rect.anchorMin = Vector2.zero;
            rect.anchorMax = Vector2.zero;
            rect.anchoredPosition = new Vector2(18f, 22f);
            rect.sizeDelta = new Vector2(92f, 30f);
            rect.pivot = Vector2.zero;

            m_channelText.transform.SetParent(buttonObject.transform, worldPositionStays: false);
            RectTransform textRect = m_channelText.rectTransform;
            textRect.anchorMin = Vector2.zero;
            textRect.anchorMax = Vector2.one;
            textRect.anchoredPosition = Vector2.zero;
            textRect.offsetMin = new Vector2(4f, 0f);
            textRect.offsetMax = new Vector2(-18f, 0f);
            textRect.pivot = new Vector2(0.5f, 0.5f);

            m_channelText.fontSize = 15f;
            m_channelText.fontStyle = FontStyles.Bold;
            m_channelText.alignment = TextAlignmentOptions.Center;
            m_channelText.raycastTarget = false;

            m_channelBackground = buttonObject.GetComponent<Image>();
            buttonObject.BindEvent(_ => toggleChatType());

            createChannelChevron(buttonObject.transform);
        }

        private void createChannelChevron(Transform parent)
        {
            GameObject chevronObject = new GameObject("Chevron", typeof(RectTransform));
            chevronObject.transform.SetParent(parent, worldPositionStays: false);

            RectTransform chevronRect = chevronObject.GetComponent<RectTransform>();
            chevronRect.anchorMin = new Vector2(1f, 0.5f);
            chevronRect.anchorMax = new Vector2(1f, 0.5f);
            chevronRect.anchoredPosition = new Vector2(-13f, 0f);
            chevronRect.sizeDelta = new Vector2(10f, 8f);
            chevronRect.pivot = new Vector2(0.5f, 0.5f);

            for (int i = 0; i < m_channelChevron.Length; ++i)
            {
                GameObject segmentObject = new GameObject($"Segment{i + 1}", typeof(RectTransform), typeof(Image));
                segmentObject.transform.SetParent(chevronObject.transform, worldPositionStays: false);

                RectTransform segmentRect = segmentObject.GetComponent<RectTransform>();
                segmentRect.anchorMin = new Vector2(0.5f, 0.5f);
                segmentRect.anchorMax = new Vector2(0.5f, 0.5f);
                segmentRect.anchoredPosition = new Vector2(i == 0 ? -2.2f : 2.2f, 0f);
                segmentRect.sizeDelta = new Vector2(6.5f, 1.5f);
                segmentRect.localRotation = Quaternion.Euler(0f, 0f, i == 0 ? -40f : 40f);

                m_channelChevron[i] = segmentObject.GetComponent<Image>();
                m_channelChevron[i].raycastTarget = false;
            }
        }

        private void createInputField()
        {
            GameObject inputObject = new GameObject("ChatInput", typeof(RectTransform), typeof(Image), typeof(TMP_InputField));
            inputObject.transform.SetParent(transform, worldPositionStays: false);

            RectTransform rect = inputObject.GetComponent<RectTransform>();
            rect.anchorMin = Vector2.zero;
            rect.anchorMax = Vector2.zero;
            rect.anchoredPosition = new Vector2(116f, 22f);
            rect.sizeDelta = new Vector2(406f, 30f);
            rect.pivot = Vector2.zero;

            Image background = inputObject.GetComponent<Image>();
            background.color = new Color(0.01f, 0.015f, 0.025f, 0.94f);

            Outline outline = inputObject.AddComponent<Outline>();
            outline.effectColor = new Color(0.25f, 0.68f, 1f, 0.7f);
            outline.effectDistance = new Vector2(1f, -1f);

            m_inputField = inputObject.GetComponent<TMP_InputField>();
            m_inputField.targetGraphic = background;
            m_inputField.characterLimit = 256;
            m_inputField.lineType = TMP_InputField.LineType.SingleLine;
            m_inputField.richText = false;
            m_inputField.onSelect.AddListener(_ => setInputFocus(true));
            m_inputField.onDeselect.AddListener(_ => setInputFocus(false));
            m_inputField.onSubmit.AddListener(_ => sendChat());

            TextMeshProUGUI text = createInputText("Text", "", Color.white);
            m_placeholderText = createInputText("Placeholder", "메시지를 입력하세요", new Color(0.62f, 0.67f, 0.74f, 0.85f));

            m_inputField.textComponent = text;
            m_inputField.placeholder = m_placeholderText;
            m_inputObject = inputObject;
            m_inputObject.SetActive(false);
        }

        private TextMeshProUGUI createInputText(string name, string value, Color color)
        {
            GameObject textObject = new GameObject(name, typeof(RectTransform), typeof(TextMeshProUGUI));
            textObject.transform.SetParent(m_inputField.transform, worldPositionStays: false);

            RectTransform rect = textObject.GetComponent<RectTransform>();
            rect.anchorMin = Vector2.zero;
            rect.anchorMax = Vector2.one;
            rect.offsetMin = new Vector2(10f, 2f);
            rect.offsetMax = new Vector2(-10f, -2f);

            TextMeshProUGUI text = textObject.GetComponent<TextMeshProUGUI>();
            text.font = m_channelText != null && m_channelText.font != null
                ? m_channelText.font
                : TMP_Settings.defaultFontAsset;
            text.fontSize = 16;
            text.color = color;
            text.text = value;
            text.alignment = TextAlignmentOptions.MidlineLeft;
            text.raycastTarget = false;
            return text;
        }

        private void toggleChatType()
        {
            switch (m_chatType)
            {
                case ChatType.StageChannel: m_chatType = ChatType.GameServer; break;
                case ChatType.GameServer:   m_chatType = ChatType.Global; break;
                default:                    m_chatType = ChatType.StageChannel; break;
            }

            updateChatTypeVisual();
        }

        private void updateChatTypeVisual()
        {
            Color chatTypeColor = getChatTypeColor(m_chatType);
            if (m_channelText != null)
            {
                m_channelText.text = getChatTypeName(m_chatType);
                m_channelText.color = chatTypeColor;
            }

            if (m_channelBackground != null)
                m_channelBackground.color = new Color(chatTypeColor.r * 0.22f, chatTypeColor.g * 0.22f, chatTypeColor.b * 0.22f, 0.95f);

            foreach (Image segment in m_channelChevron)
            {
                if (segment != null)
                    segment.color = chatTypeColor;
            }

            if (m_placeholderText != null)
                m_placeholderText.text = $"{getChatTypeName(m_chatType)} 메시지 입력  (/w 캐릭터이름 메시지)";
        }

        private void updateEditingVisual()
        {
            if (m_panelBackground != null)
                m_panelBackground.color = m_isEditing ? s_panelEditingColor : s_panelIdleColor;

            if (m_panelAccent != null)
                m_panelAccent.gameObject.SetActive(m_isEditing);

            if (m_scrollRect != null)
                m_scrollRect.enabled = m_isEditing;

            if (m_viewportRaycastTarget != null)
                m_viewportRaycastTarget.raycastTarget = m_isEditing;

            if (m_scrollbarObject != null)
                m_scrollbarObject.SetActive(m_isEditing);

            foreach (UI_ChatLine line in m_lines)
            {
                if (line != null)
                    line.SetExpanded(m_isEditing);
            }
        }

        private void setInputFocus(bool focused)
        {
            Managers.Managers.Input.SetKeyboardGameplayBlocked(focused);
        }

        private void focusInput()
        {
            if (!m_isEditing || m_inputField == null)
                return;

            m_inputField.ActivateInputField();
            m_inputField.Select();
        }

        private void openInput()
        {
            if (m_isEditing || m_inputObject == null || m_inputField == null)
                return;

            m_isEditing = true;
            m_inputObject.SetActive(true);
            Managers.Managers.Input.SetMenuInputBlocked(true);
            focusInput();
            m_scrollToLatestPending = true;
            updateEditingVisual();
        }

        private void closeInput()
        {
            if (!m_isEditing)
                return;

            m_isEditing = false;
            m_inputField.text = string.Empty;
            m_inputField.DeactivateInputField();
            m_inputObject.SetActive(false);
            setInputFocus(false);
            Managers.Managers.Input.SetMenuInputBlocked(false);
            updateEditingVisual();
        }

        private void sendChat()
        {
            string message = m_inputField.text.Trim();
            if (string.IsNullOrEmpty(message))
                return;

            if (Encoding.UTF8.GetByteCount(message) > 256)
            {
                addSystemMessage("메시지는 UTF-8 기준 256바이트까지 입력할 수 있습니다.");
                return;
            }

            string targetName = string.Empty;
            ChatType chatType = m_chatType;
            if (message == "/w" || message.StartsWith("/w "))
            {
                string[] parts = message.Split(' ', 3, System.StringSplitOptions.RemoveEmptyEntries);
                if (parts.Length != 3)
                {
                    addSystemMessage("사용법: /w 캐릭터이름 메시지");
                    return;
                }

                chatType = ChatType.Whisper;
                targetName = parts[1];
                message = parts[2];
            }

            NetworkManager network = NetworkManager.Instance;
            if (network == null || !network.IsConnected)
            {
                addSystemMessage("게임 서버에 연결되어 있지 않습니다.");
                return;
            }

            network.Send(GamePacketId.ChatSendReq, new ChatSendReq
            {
                ChatType = chatType,
                Message = message,
                TargetName = targetName,
            });
            m_inputField.text = string.Empty;
            focusInput();
        }
    }
}
