using System;
using UnityEngine;
using UnityEngine.EventSystems;

namespace Client.UI
{
    // UI 요소(Button, Image, Panel 등)에 붙어서 Pointer 이벤트를 Action 델리게이트로 변환해주는 컴포넌트.
    //
    // Unity 의 IPointerClickHandler, IDragHandler 등을 구현하면 자동으로 호출되는데,
    // 그걸 외부에서 등록할 수 있는 Action 델리게이트로 노출.
    //
    // 사용자가 직접 AddComponent 할 일은 없음. UI_Base.BindEvent 가 자동으로 부착.
    public class UI_EventHandler : MonoBehaviour, IPointerClickHandler, IDragHandler
    {
        public Action<PointerEventData> OnClickHandler;
        public Action<PointerEventData> OnDragHandler;

        public void OnPointerClick(PointerEventData eventData)
        {
            OnClickHandler?.Invoke(eventData);
        }

        public void OnDrag(PointerEventData eventData)
        {
            OnDragHandler?.Invoke(eventData);
        }
    }
}
