using UnityEngine;

namespace Client.UI
{
    // Popup 형태 UI 의 베이스.
    //
    // UIManager 의 Popup Stack 에 push 되어 관리됨.
    // ESC 등으로 가장 위 popup 이 닫히는 흐름은 UIManager 가 담당.
    //
    // Rookiss 원본은 여기서 Managers.UI.ClosePopupUI(this) 를 호출했지만,
    // 우리는 어셈블리 순환 의존을 피하려고 닫기 동작을 UIManager 쪽에서만 한다.
    // (UI 어셈블리가 Managers 를 참조하지 않게 하기 위함)
    //
    // popup 이 스스로 닫고 싶을 때:
    //   - 호출하는 쪽에서 Managers.UI.ClosePopupUI(this) 호출
    //   - 또는 OnCloseRequested 이벤트를 발행 (UIManager 가 구독)
    public class UI_Popup : UI_Base
    {
        // 자식 클래스가 Init() override 시 base.Init() 을 호출해야 함.
        // (Canvas 의 sorting order 같은 popup 공통 세팅이 여기에 들어갈 예정)
        public override void Init()
        {
            // 현재 시점에는 UI_Popup 만의 공통 초기화는 없음.
            // sorting order 세팅은 UIManager.ShowPopupUI 가 처리.
        }
    }
}
