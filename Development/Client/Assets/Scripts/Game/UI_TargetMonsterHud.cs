using Client.UI;
using GameData;
using TMPro;
using UnityEngine;
using UnityEngine.UI;

namespace Client.Game
{
    // 로컬 플레이어가 현재 공격 중인 몬스터의 이름과 HP를 표시하는 상단 중앙 HUD.
    // 서버가 확정한 SkillDamageNtf만 대상으로 삼으며, 광역 공격 시 잦은 교체를 막기 위해
    // 새 대상 표시 후 1초 동안 다른 대상으로 바뀌지 않는다.
    public class UI_TargetMonsterHud : UI_Scene
    {
        private const float k_switchLockSec = 1f;
        private const float k_hideDistance = 50f;
        private const float k_hideDistanceSqr = k_hideDistance * k_hideDistance;

        private MonsterObject m_target;
        private float m_switchUnlockedAt;

        private CanvasGroup m_canvasGroup;
        private TextMeshProUGUI m_nameText;
        private TextMeshProUGUI m_hpText;
        private Image m_hpFill;

        public override void Init()
        {
            base.Init();
            buildVisual();
            hide();

            if (SkillSystem.Instance != null)
                SkillSystem.Instance.LocalPlayerDamagedMonster += onLocalPlayerDamagedMonster;
        }

        private void OnDestroy()
        {
            if (SkillSystem.Instance != null)
                SkillSystem.Instance.LocalPlayerDamagedMonster -= onLocalPlayerDamagedMonster;
        }

        private void Update()
        {
            if (!isCurrentTargetValid())
            {
                hide();
                return;
            }

            refresh();
        }

        private void onLocalPlayerDamagedMonster(MonsterObject monster)
        {
            if (!isCurrentTargetValid())
                hide();

            if (!isTargetValid(monster))
                return;

            if (m_target != null && m_target != monster && Time.unscaledTime < m_switchUnlockedAt)
                return;

            if (m_target != monster)
            {
                m_target = monster;
                m_switchUnlockedAt = Time.unscaledTime + k_switchLockSec;
                m_canvasGroup.alpha = 1f;
            }

            refresh();
        }

        private bool isCurrentTargetValid()
        {
            return isTargetValid(m_target);
        }

        private static bool isTargetValid(MonsterObject target)
        {
            if (target == null || target.IsDead || target.CurHp <= 0.0)
                return false;

            StageManager stage = StageManager.Instance;
            PlayerCharacter player = stage != null ? stage.LocalPlayer : null;
            if (stage == null || player == null || stage.IsStageLoading)
                return false;
            if (stage.FindActor(target.ObjectId) != target)
                return false;

            Vector3 delta = player.transform.position - target.transform.position;
            float distanceSqr = delta.x * delta.x + delta.z * delta.z;
            return distanceSqr <= k_hideDistanceSqr;
        }

        private void refresh()
        {
            double maxHp = m_target.MaxHp;
            float ratio = maxHp > 0.0 ? Mathf.Clamp01((float)(m_target.CurHp / maxHp)) : 0f;

            GameData_Monster data = GameDataTable_Monster.FindData(m_target.MonsterKey);
            m_nameText.text = data != null ? data.Name : $"Monster {m_target.MonsterKey}";
            m_hpText.text = Mathf.RoundToInt((float)m_target.CurHp) + " / " + Mathf.RoundToInt((float)maxHp);
            m_hpFill.rectTransform.localScale = new Vector3(ratio, 1f, 1f);
        }

        private void hide()
        {
            m_target = null;
            m_switchUnlockedAt = 0f;
            if (m_canvasGroup != null)
                m_canvasGroup.alpha = 0f;
        }

        // 프리팹은 Canvas 진입점만 제공하고, 단순한 placeholder 모양은 코드로 구성한다.
        // 추후 아트가 확정되면 자식 구성만 프리팹으로 옮기고 대상 선택 로직은 그대로 유지할 수 있다.
        private void buildVisual()
        {
            CanvasScaler scaler = gameObject.GetComponent<CanvasScaler>();
            if (scaler == null)
                scaler = gameObject.AddComponent<CanvasScaler>();
            scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
            scaler.referenceResolution = new Vector2(1920f, 1080f);

            m_canvasGroup = gameObject.GetComponent<CanvasGroup>();
            if (m_canvasGroup == null)
                m_canvasGroup = gameObject.AddComponent<CanvasGroup>();
            m_canvasGroup.interactable = false;
            m_canvasGroup.blocksRaycasts = false;

            TMP_FontAsset font = Resources.Load<TMP_FontAsset>("Font/NanumGothic SDF");

            GameObject panel = new GameObject("Panel", typeof(RectTransform), typeof(Image));
            panel.transform.SetParent(transform, false);
            RectTransform panelRect = panel.GetComponent<RectTransform>();
            panelRect.anchorMin = new Vector2(0.5f, 1f);
            panelRect.anchorMax = new Vector2(0.5f, 1f);
            panelRect.pivot = new Vector2(0.5f, 1f);
            panelRect.anchoredPosition = new Vector2(0f, -16f);
            panelRect.sizeDelta = new Vector2(620f, 56f);
            Image panelImage = panel.GetComponent<Image>();
            panelImage.color = new Color(0.03f, 0.035f, 0.05f, 0.5f);
            panelImage.raycastTarget = false;

            m_nameText = createText("MonsterNameText", panel.transform, font, 25f, FontStyles.Bold);
            RectTransform nameRect = m_nameText.rectTransform;
            nameRect.anchorMin = Vector2.zero;
            nameRect.anchorMax = new Vector2(0.36f, 1f);
            nameRect.offsetMin = new Vector2(16f, 0f);
            nameRect.offsetMax = new Vector2(-8f, 0f);

            GameObject hpBackground = new GameObject("HpBarBackground", typeof(RectTransform), typeof(Image));
            hpBackground.transform.SetParent(panel.transform, false);
            RectTransform hpRect = hpBackground.GetComponent<RectTransform>();
            hpRect.anchorMin = new Vector2(0.36f, 0.5f);
            hpRect.anchorMax = new Vector2(1f, 0.5f);
            hpRect.offsetMin = new Vector2(8f, -15f);
            hpRect.offsetMax = new Vector2(-16f, 15f);
            Image hpBackgroundImage = hpBackground.GetComponent<Image>();
            hpBackgroundImage.color = new Color(0.12f, 0.025f, 0.025f, 0.6f);
            hpBackgroundImage.raycastTarget = false;

            GameObject hpFill = new GameObject("HpFill", typeof(RectTransform), typeof(Image));
            hpFill.transform.SetParent(hpBackground.transform, false);
            RectTransform fillRect = hpFill.GetComponent<RectTransform>();
            fillRect.anchorMin = Vector2.zero;
            fillRect.anchorMax = Vector2.one;
            fillRect.offsetMin = new Vector2(2f, 2f);
            fillRect.offsetMax = new Vector2(-2f, -2f);
            fillRect.pivot = new Vector2(0f, 0.5f);
            m_hpFill = hpFill.GetComponent<Image>();
            m_hpFill.color = new Color(0.82f, 0.08f, 0.08f, 1f);
            m_hpFill.raycastTarget = false;

            m_hpText = createText("HpText", hpBackground.transform, font, 19f, FontStyles.Bold);
            RectTransform hpTextRect = m_hpText.rectTransform;
            hpTextRect.anchorMin = Vector2.zero;
            hpTextRect.anchorMax = Vector2.one;
            hpTextRect.offsetMin = Vector2.zero;
            hpTextRect.offsetMax = Vector2.zero;
        }

        private static TextMeshProUGUI createText(string name, Transform parent, TMP_FontAsset font, float size, FontStyles style)
        {
            GameObject textObject = new GameObject(name, typeof(RectTransform), typeof(TextMeshProUGUI));
            textObject.transform.SetParent(parent, false);
            TextMeshProUGUI text = textObject.GetComponent<TextMeshProUGUI>();
            if (font != null)
                text.font = font;
            text.fontSize = size;
            text.fontStyle = style;
            text.alignment = TextAlignmentOptions.Center;
            text.color = Color.white;
            text.raycastTarget = false;
            return text;
        }
    }
}
