using Client.UI;
using UnityEngine;
using UnityEngine.UI;

namespace Client.Game
{
    // 감정표현 선택 팝업. 열리면 AnimStates.Dances + Emojis 를 그리드 버튼으로 자동 생성한다.
    // 버튼 클릭 시 로컬 감정 재생(+서버 통보) 후 패널을 닫는다. (감정 추가 시 코드 수정 불필요 — 목록만 늘리면 됨)
    //
    // 프리팹 경로: Resources/UI/Popup/UI_EmotePanel
    //   자식 이름(Bind):
    //     GameObject : Grid        (GridLayoutGroup 컨테이너. 여기에 버튼이 쌓인다)
    //     Button     : CloseButton (배경/닫기 버튼)
    public class UI_EmotePanel : UI_Popup
    {
        private enum Objects { Grid }
        private enum Buttons { CloseButton }

        public override void Init()
        {
            base.Init();

            Bind<GameObject>(typeof(Objects));
            Bind<Button>(typeof(Buttons));

            Button closeBtn = Get<Button>((int)Buttons.CloseButton);
            if (closeBtn != null)
                closeBtn.gameObject.BindEvent(_ => closePanel());

            buildButtons();
        }

        private void buildButtons()
        {
            GameObject grid = Get<GameObject>((int)Objects.Grid);
            Transform parent = (grid != null) ? grid.transform : transform;

            foreach (string s in AnimStates.Dances)
                makeButton(parent, s);
            foreach (string s in AnimStates.Emojis)
                makeButton(parent, s);
        }

        private void makeButton(Transform parent, string emoteState)
        {
            UI_EmoteButton btn = Managers.Managers.UI.MakeSubItem<UI_EmoteButton>(parent);
            if (btn == null)
                return;   // 프리팹(Resources/UI/SubItem/UI_EmoteButton) 미작성 시 스킵.

            btn.Init();   // MakeSubItem 은 Init 을 호출하지 않으므로 여기서 1회 바인딩.
            btn.Set(emoteState, prettify(emoteState), closePanel);
        }

        // "Emoji_Be_Bashful" -> "Be Bashful", "Dance_1" -> "Dance 1"
        private static string prettify(string state)
        {
            return state.Replace("Emoji_", "").Replace("Dance_", "Dance ").Replace("_", " ");
        }

        private void closePanel()
        {
            Managers.Managers.UI.ClosePopupUI(this);
        }
    }
}
