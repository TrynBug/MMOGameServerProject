using TMPro;
using UnityEngine;
using UnityEngine.UI;

namespace Client.UI
{
    // 버프 1개를 표시하는 SubItem UI (순수 표시용).
    //
    // 게임 상태를 모른다 — 부모(UI_BuffBar)가 계산한 값을 SetData 로 받아 그리기만 한다.
    // 그래서 UI 어셈블리(최하위, Game 을 모름)에 둘 수 있다.
    //
    // 프리팹 경로: Resources/UI/SubItem/UI_BuffIcon
    // 프리팹 자식 GameObject 이름이 아래 enum 과 정확히 일치해야 Bind 된다:
    //   Image           : IconImage              (배경 겸 아이콘. 아트 없으면 색만 적용)
    //   TextMeshProUGUI : NameText, TimeText, StackText
    public class UI_BuffIcon : UI_Base
    {
        private enum Images { IconImage }
        private enum Texts { NameText, TimeText, StackText }

        public override void Init()
        {
            Bind<Image>(typeof(Images));
            Bind<TextMeshProUGUI>(typeof(Texts));
        }

        // 부모(UI_BuffBar)가 매 프레임 호출.
        // icon 이 null 이면 색(bgColor)만 적용한다(아이콘 아트 미적용 단계).
        public void SetData(string name, int stackCount, string timeText, Color bgColor, Sprite icon = null)
        {
            Image image = Get<Image>((int)Images.IconImage);
            if (image != null)
            {
                if (icon != null)
                    image.sprite = icon;
                image.color = bgColor;
            }

            TextMeshProUGUI nameText = Get<TextMeshProUGUI>((int)Texts.NameText);
            if (nameText != null)
                nameText.text = name;

            TextMeshProUGUI remainText = Get<TextMeshProUGUI>((int)Texts.TimeText);
            if (remainText != null)
                remainText.text = timeText;

            TextMeshProUGUI stackText = Get<TextMeshProUGUI>((int)Texts.StackText);
            if (stackText != null)
                stackText.text = (stackCount > 1) ? $"x{stackCount}" : string.Empty;
        }
    }
}
