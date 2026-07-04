using System;
using Client.UI;
using TMPro;
using UnityEngine;

namespace Client.Game
{
    // 감정표현 1개 버튼 (SubItem). 클릭 시 로컬 플레이어가 해당 감정을 재생 + 서버 통보(관전자 relay).
    //
    // 게임 상태(StageManager/PlayerCharacter)를 읽으므로 Game 어셈블리에 둔다. (UI_PlayerHud 와 동일 사상)
    //
    // 프리팹 경로: Resources/UI/SubItem/UI_EmoteButton
    //   루트: Button 컴포넌트
    //   자식 이름(Bind): Label (TextMeshProUGUI)
    public class UI_EmoteButton : UI_Base
    {
        private enum Texts { Label }

        private string m_emoteState;
        private Action m_onClicked;

        public override void Init()
        {
            Bind<TextMeshProUGUI>(typeof(Texts));
            gameObject.BindEvent(_ => onClick());
        }

        // 부모(UI_EmotePanel)가 호출. label = 표시 텍스트, emoteState = 재생할 Animator 상태명, onClicked = 클릭 후 콜백(패널 닫기).
        public void Set(string emoteState, string label, Action onClicked)
        {
            m_emoteState = emoteState;
            m_onClicked = onClicked;

            TextMeshProUGUI t = Get<TextMeshProUGUI>((int)Texts.Label);
            if (t != null)
                t.text = label;
        }

        private void onClick()
        {
            PlayerCharacter local = StageManager.Instance != null ? StageManager.Instance.LocalPlayer : null;
            if (local != null && !string.IsNullOrEmpty(m_emoteState))
                local.LocalEmote(m_emoteState);   // 재생 + 서버 통보(ActorActionReq)

            m_onClicked?.Invoke();                 // 패널 닫기 등
        }
    }
}
