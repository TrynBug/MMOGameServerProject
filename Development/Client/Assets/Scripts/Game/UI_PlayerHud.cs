using Client.UI;
using GameData;
using TMPro;
using UnityEngine;
using UnityEngine.UI;

namespace Client.Game
{
    // 인게임 플레이어 HUD (HP/MP 바 + 스킬 슬롯). 정식 UI 파이프라인.
    //
    // UI_Scene 으로서 UIManager.ShowSceneUI<UI_PlayerHud>() 로 표시된다. (UI_BuffBar 와 동일 사상)
    // 매 프레임 LocalPlayer 의 HP/MP 와 SkillSystem 의 슬롯/쿨다운을 읽어 그린다.
    //
    // 게임 상태(StageManager/SkillSystem/GameData)를 읽으므로 Game 어셈블리에 둔다.
    // (표시만 하는 UI_SkillSlot 은 UI 어셈블리에 있음.)
    //
    // 프리팹 경로: Resources/UI/Scene/UI_PlayerHud
    // 프리팹 자식 GameObject 이름이 아래 enum 과 일치해야 Bind 된다:
    //   Image           : HpFill (Filled/Horizontal), MpFill (Filled/Horizontal)
    //   TextMeshProUGUI : HpText, MpText
    //   GameObject      : SkillSlotContainer  (HorizontalLayoutGroup. 여기에 슬롯이 쌓인다)
    public class UI_PlayerHud : UI_Scene
    {
        private enum Images { HpFill, MpFill }
        private enum Texts { HpText, MpText }
        private enum Objects { SkillSlotContainer }

        private Image m_hpFill;
        private Image m_mpFill;
        private TextMeshProUGUI m_hpText;
        private TextMeshProUGUI m_mpText;

        private UI_SkillSlot[] m_slots;

        // skillKey → 아이콘 스프라이트 캐시 (매 프레임 Resources.Load 방지). null 도 캐싱(미지정/실패 슬롯).
        private readonly System.Collections.Generic.Dictionary<int, Sprite> m_iconCache
            = new System.Collections.Generic.Dictionary<int, Sprite>();

        public override void Init()
        {
            base.Init();

            Bind<Image>(typeof(Images));
            Bind<TextMeshProUGUI>(typeof(Texts));
            Bind<GameObject>(typeof(Objects));

            m_hpFill = Get<Image>((int)Images.HpFill);
            m_mpFill = Get<Image>((int)Images.MpFill);
            m_hpText = Get<TextMeshProUGUI>((int)Texts.HpText);
            m_mpText = Get<TextMeshProUGUI>((int)Texts.MpText);

            buildSkillSlots();
        }

        // SkillSlotContainer 아래에 SkillSlotCount 개 슬롯을 1회 생성한다 (UI_BuffBar 가 아이콘을 만드는 방식).
        private void buildSkillSlots()
        {
            int count = SkillSystem.SkillSlotCount;
            m_slots = new UI_SkillSlot[count];

            GameObject container = Get<GameObject>((int)Objects.SkillSlotContainer);
            Transform parent = (container != null) ? container.transform : transform;

            for (int i = 0; i < count; i++)
            {
                UI_SkillSlot slot = Managers.Managers.UI.MakeSubItem<UI_SkillSlot>(parent);
                if (slot == null)
                    continue;   // 프리팹(Resources/UI/SubItem/UI_SkillSlot) 미작성 시 스킵.

                slot.Init();    // MakeSubItem 은 Init 을 호출하지 않으므로 여기서 1회 바인딩.
                m_slots[i] = slot;
            }
        }

        private void Update()
        {
            PlayerCharacter player = StageManager.Instance != null ? StageManager.Instance.LocalPlayer : null;
            if (player == null)
                return;

            updateBars(player);
            updateSkillSlots(player);
        }

        private void updateBars(PlayerCharacter player)
        {
            double maxHp = player.MaxHp;
            double maxMp = player.MaxMp;

            if (m_hpFill != null)
                m_hpFill.fillAmount = (maxHp > 0.0) ? Mathf.Clamp01((float)(player.CurHp / maxHp)) : 0f;
            if (m_mpFill != null)
                m_mpFill.fillAmount = (maxMp > 0.0) ? Mathf.Clamp01((float)(player.CurMp / maxMp)) : 0f;

            if (m_hpText != null)
                m_hpText.text = Mathf.RoundToInt((float)player.CurHp) + " / " + Mathf.RoundToInt((float)maxHp);
            if (m_mpText != null)
                m_mpText.text = Mathf.RoundToInt((float)player.CurMp) + " / " + Mathf.RoundToInt((float)maxMp);
        }

        private void updateSkillSlots(PlayerCharacter player)
        {
            SkillSystem sys = SkillSystem.Instance;
            if (sys == null || m_slots == null)
                return;

            for (int i = 0; i < m_slots.Length; i++)
            {
                UI_SkillSlot slot = m_slots[i];
                if (slot == null)
                    continue;

                int skillKey = sys.GetSlotSkillKey(i);
                GameData_Skill skill = (skillKey != 0) ? GameDataTable_Skill.FindData(skillKey) : null;

                string keyLabel = (i + 1).ToString();   // OnSkill1~4 단축키.
                string name = (skill != null) ? skill.Name : string.Empty;
                Sprite icon = loadIcon(skillKey, skill);
                float ratio = sys.GetCooldownRatio(skillKey);
                float sec = sys.GetCooldownRemainSec(skillKey);
                bool manaOk = (skill == null) || (player.CurMp >= skill.ManaCost);

                slot.SetData(keyLabel, name, icon, ratio, sec, manaOk);
            }
        }

        // 스킬 데이터의 Icon 경로(Resources 상대)로 스프라이트를 로드해 캐싱한다.
        // Icon 이 비었거나 로드 실패면 null(슬롯은 바탕색만 표시).
        private Sprite loadIcon(int skillKey, GameData_Skill skill)
        {
            if (m_iconCache.TryGetValue(skillKey, out Sprite cached))
                return cached;

            Sprite sprite = null;
            if (skill != null && !string.IsNullOrEmpty(skill.Icon))
                sprite = Managers.Managers.Resource.Load<Sprite>(skill.Icon);

            m_iconCache[skillKey] = sprite;
            return sprite;
        }
    }
}
