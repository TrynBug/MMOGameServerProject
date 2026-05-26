using TMPro;

namespace Client.UI
{
    // 캐릭터 선택 화면 UI. 현재는 최소 UI ("로딩 중..." 정도) 만 제공.
    //
    // 향후 캐릭터 슬롯, 직업 선택, 캐릭터 생성 다이얼로그 등이 추가될 예정.
    // 그때 이 클래스의 Bind 대상 enum 들을 확장.
    public class UI_CharacterSelection : UI_Scene
    {
        private enum Texts
        {
            StatusText,
        }

        public override void Init()
        {
            base.Init();

            Bind<TextMeshProUGUI>(typeof(Texts));
        }

        public void SetStatus(string message)
        {
            TextMeshProUGUI text = Get<TextMeshProUGUI>((int)Texts.StatusText);
            if (text != null) text.text = message;
        }
    }
}
