using System;
using UnityEngine.UI;

namespace Client.UI
{
    // 게임 중 Esc 로 여는 메뉴 팝업.
    //
    // 책임: 버튼 클릭을 원시 이벤트로 외부(GameScene)에 발행.
    // 책임이 아닌 것: 실제 동작(연결 끊기, 씬 전환, 패킷 송신) — GameScene 이 처리.
    public class UI_GameMenu : UI_Popup
    {
        private enum Buttons { ResumeButton, CharacterSelectButton, LogoutButton }

        public event Action OnResume;            // 계속하기 (메뉴 닫기)
        public event Action OnCharacterSelect;   // 캐릭터 선택으로 돌아가기
        public event Action OnLogout;            // 로그인으로 돌아가기

        public override void Init()
        {
            base.Init();

            Bind<Button>(typeof(Buttons));
            GetButton((int)Buttons.ResumeButton).gameObject.BindEvent(_ => OnResume?.Invoke());
            GetButton((int)Buttons.CharacterSelectButton).gameObject.BindEvent(_ => OnCharacterSelect?.Invoke());
            GetButton((int)Buttons.LogoutButton).gameObject.BindEvent(_ => OnLogout?.Invoke());
        }
    }
}
