using Client.Managers;
using UnityEngine.UI;

namespace Client.UI
{
    // 화면 우상단에 항상 떠있는 원형 "환경설정" 버튼 오버레이.
    //
    // Esc 메뉴가 없는 씬(로그인, 캐릭터선택)에서 환경설정을 열 수 있게 한다.
    // 클릭하면 UI_Settings 팝업을 띄우고, 팝업의 닫기(OnClose)로 닫는다.
    //
    // 자기완결형: 씬은 UIManager.ShowOverlayUI<UI_SettingsButton>() 로 띄우기만 하면 된다.
    // (씬 전환 시 @UI_Root 와 함께 파괴되므로 별도 정리 불필요.)
    public class UI_SettingsButton : UI_Scene
    {
        private enum Buttons { SettingsButton }

        private UI_Settings m_settings;

        public override void Init()
        {
            base.Init();

            Bind<Button>(typeof(Buttons));
            GetButton((int)Buttons.SettingsButton).gameObject.BindEvent(_ => openSettings());
        }

        private void openSettings()
        {
            if (m_settings != null) return;   // 이미 열려 있음(팝업 Dim 이 버튼을 덮으므로 사실상 도달 안 함, 안전장치)
            m_settings = Managers.Managers.UI.ShowPopupUI<UI_Settings>();
            if (m_settings == null) return;
            m_settings.OnClose += closeSettings;
        }

        private void closeSettings()
        {
            if (m_settings == null) return;
            m_settings.OnClose -= closeSettings;
            Managers.Managers.UI.ClosePopupUI(m_settings);
            m_settings = null;
        }
    }
}
