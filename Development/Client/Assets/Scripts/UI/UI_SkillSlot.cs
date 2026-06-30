using TMPro;
using UnityEngine;
using UnityEngine.UI;

namespace Client.UI
{
    // 스킬 슬롯 1개를 표시하는 SubItem UI (순수 표시용).
    //
    // 게임 상태를 모른다 — 부모(UI_PlayerHud)가 계산한 값을 SetData 로 받아 그리기만 한다.
    // 그래서 UI 어셈블리(최하위, Game 을 모름)에 둘 수 있다. (UI_BuffIcon 과 동일 사상)
    //
    // 프리팹 경로: Resources/UI/SubItem/UI_SkillSlot
    // 프리팹 자식 GameObject 이름이 아래 enum 과 정확히 일치해야 Bind 된다:
    //   Image           : IconImage         (슬롯 배경 프레임. 아이콘 투명영역/빈 슬롯에서 보이는 바탕)
    //                      SkillIcon         (실제 스킬 아이콘 스프라이트. 없으면 비활성)
    //                      CooldownOverlay   (Filled/Radial360. 쿨다운 비율만큼 어둡게 덮음)
    //   TextMeshProUGUI : KeyText            (단축키 숫자 1~4)
    //                      NameText          (스킬 이름 — 아이콘 없을 때만 표시)
    //                      CooldownText      (남은 쿨다운 초)
    public class UI_SkillSlot : UI_Base
    {
        private enum Images { IconImage, SkillIcon, CooldownOverlay }
        private enum Texts { KeyText, NameText, CooldownText }

        // 슬롯 배경색 (placeholder). 마나 부족 시 어둡게 틴트.
        private static readonly Color k_ready = new Color(0.16f, 0.18f, 0.26f, 0.92f);
        private static readonly Color k_noMana = new Color(0.10f, 0.10f, 0.14f, 0.92f);
        // 아이콘 스프라이트 틴트. 준비=흰색(원색), 마나부족=흐린 회색.
        private static readonly Color k_iconReady = Color.white;
        private static readonly Color k_iconNoMana = new Color(0.45f, 0.45f, 0.5f, 1f);

        private Image m_icon;
        private Image m_skillIcon;
        private Image m_cooldownOverlay;
        private TextMeshProUGUI m_keyText;
        private TextMeshProUGUI m_nameText;
        private TextMeshProUGUI m_cooldownText;

        public override void Init()
        {
            Bind<Image>(typeof(Images));
            Bind<TextMeshProUGUI>(typeof(Texts));

            m_icon = Get<Image>((int)Images.IconImage);
            m_skillIcon = Get<Image>((int)Images.SkillIcon);
            m_cooldownOverlay = Get<Image>((int)Images.CooldownOverlay);
            m_keyText = Get<TextMeshProUGUI>((int)Texts.KeyText);
            m_nameText = Get<TextMeshProUGUI>((int)Texts.NameText);
            m_cooldownText = Get<TextMeshProUGUI>((int)Texts.CooldownText);
        }

        // 부모(UI_PlayerHud)가 매 프레임 호출.
        //   icon          : 스킬 아이콘 스프라이트 (null 이면 바탕색만 — 빈/미지정 슬롯).
        //   cooldownRatio : 1=방금 시전, 0=준비완료 (라디얼 fillAmount).
        //   cooldownSec   : 남은 쿨다운 초 (표시용).
        //   manaOk        : 마나 충분 여부 (부족하면 어둡게).
        public void SetData(string keyLabel, string skillName, Sprite icon, float cooldownRatio, float cooldownSec, bool manaOk)
        {
            if (m_keyText != null)
                m_keyText.text = keyLabel;

            // 아이콘이 있으면 이름은 숨겨 깔끔하게(아이콘이 정체성을 대신). 없으면 이름으로 식별.
            if (m_nameText != null)
                m_nameText.text = (icon != null) ? string.Empty : skillName;

            if (m_skillIcon != null)
            {
                bool hasIcon = icon != null;
                m_skillIcon.enabled = hasIcon;
                if (hasIcon)
                {
                    m_skillIcon.sprite = icon;
                    m_skillIcon.color = manaOk ? k_iconReady : k_iconNoMana;
                }
            }

            if (m_icon != null)
                m_icon.color = manaOk ? k_ready : k_noMana;

            bool onCooldown = cooldownRatio > 0f;

            if (m_cooldownOverlay != null)
            {
                m_cooldownOverlay.gameObject.SetActive(onCooldown);
                if (onCooldown)
                    m_cooldownOverlay.fillAmount = cooldownRatio;
            }

            if (m_cooldownText != null)
            {
                if (onCooldown)
                    m_cooldownText.text = (cooldownSec >= 10f)
                        ? Mathf.CeilToInt(cooldownSec).ToString()
                        : cooldownSec.ToString("0.0");
                else
                    m_cooldownText.text = string.Empty;
            }
        }
    }
}
