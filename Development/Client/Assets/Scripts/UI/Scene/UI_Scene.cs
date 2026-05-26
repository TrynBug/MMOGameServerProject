using UnityEngine;

namespace Client.UI
{
    // 항상 화면에 떠있는 UI 의 베이스.
    // 예: HUD, 미니맵, 메인 메뉴바, 채팅창.
    //
    // Popup 과 달리 Stack 으로 관리되지 않고, 한 씬에 1개씩만 존재함 (UIManager 의 m_sceneUI).
    public class UI_Scene : UI_Base
    {
        public override void Init()
        {
            // 현재 시점에는 UI_Scene 만의 공통 초기화는 없음.
            // Canvas sorting order 세팅은 UIManager.ShowSceneUI 가 처리.
        }
    }
}
