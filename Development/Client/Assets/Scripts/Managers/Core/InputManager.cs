using System;
using UnityEngine;
using UnityEngine.InputSystem;

namespace Client.Managers
{
    // 새 Input System (InputAction) 위에 얇은 추상화 레이어.
    //
    // 책임:
    //   - GameInput (Generated 클래스) 를 소유하고 활성화/정리
    //   - 키 액션을 C# Action 델리게이트로 노출 (Rookiss 스타일 구독 패턴)
    //   - 마우스 좌표는 폴링 방식으로 제공 (이벤트가 아니라 항상 최신 값)
    //   - 액션맵 전환 (Gameplay <-> UI) 제공
    //
    // 책임이 아닌 것 (의도적):
    //   - 카메라, NavMesh, 네트워크, 캐릭터 같은 게임 로직 모름
    //   - 화면 좌표 -> 월드 좌표 변환은 MouseInputHandler 책임
    //   - "캐릭터를 어디로 보내라" 같은 도메인 의미 모름
    //
    // 사용 예:
    //   Managers.Input.OnSkill1 += () => Debug.Log("Q pressed");
    //   if (Managers.Input.IsMouseClickHeld) { var p = Managers.Input.MouseScreenPosition; ... }
    public class InputManager
    {
        private GameInput m_gameInput;

        // ─── 외부 노출 이벤트 (Rookiss 스타일 Action 델리게이트) ──────────
        //
        // performed 시점에 한 번 호출. 즉 Q를 누른 순간에 한 번.
        // 키를 떼는 시점이 필요하면 OnSkill1Released 같은 걸 추가하면 됨.

        public event Action MouseClickedDown;       // 마우스 좌클릭이 처음 눌린 순간
        public event Action MouseClickedUp;         // 마우스 좌클릭을 뗀 순간

        public event Action OnSkill1;
        public event Action OnSkill2;
        public event Action OnSkill3;
        public event Action OnSkill4;

        public event Action OnInventory;
        public event Action OnCharacterMenu;
        public event Action OnMenu;

        // ─── 폴링 방식 상태 ─────────────────────────────────────────────
        // 마우스 위치는 매 프레임 읽어야 하니 이벤트보다 폴링이 자연스러움.

        public bool IsMouseClickHeld
        {
            get
            {
                if (m_gameInput == null) return false;
                return m_gameInput.Gameplay.MouseClick.IsPressed();
            }
        }

        public Vector2 MouseScreenPosition
        {
            get
            {
                if (m_gameInput == null) return Vector2.zero;
                return m_gameInput.Gameplay.MousePosition.ReadValue<Vector2>();
            }
        }

        // 마우스 휠 스크롤 델타.
        // y: 지난 프레임 동안의 수직 스크롤 양 (앞으로 굴리면 양수, 뒤로 굴리면 음수).
        // x: 가로 스크롤. 일반 마우스에는 없으나 값은 0.
        // .inputactions 에 등록하지 않고 직접 Mouse.current 에서 읽는 이유:
        //   - 휠은 채팅창 같은 UI 상태에서도 대체로 동작해도 무방 (도메인 특성)
        //   - 구독 이벤트보다 폴링이 더 자연스러운 입력
        //   - .inputactions 수정 없이 가벼게 추가 가능
        // 채팅창 같은 UI 상태에서 휠을 막아야 하면 나중에 .inputactions 으로 승격.
        public Vector2 MouseScrollDelta
        {
            get
            {
                Mouse mouse = Mouse.current;
                if (mouse == null) return Vector2.zero;
                return mouse.scroll.ReadValue();
            }
        }

        // ─── 초기화 / 정리 ─────────────────────────────────────────────

        public void Init()
        {
            m_gameInput = new GameInput();

            // Gameplay 액션맵의 각 액션에 핸들러 등록
            m_gameInput.Gameplay.MouseClick.started   += onMouseClickStarted;
            m_gameInput.Gameplay.MouseClick.canceled  += onMouseClickCanceled;

            m_gameInput.Gameplay.Skill1.performed        += _ => OnSkill1?.Invoke();
            m_gameInput.Gameplay.Skill2.performed        += _ => OnSkill2?.Invoke();
            m_gameInput.Gameplay.Skill3.performed        += _ => OnSkill3?.Invoke();
            m_gameInput.Gameplay.Skill4.performed        += _ => OnSkill4?.Invoke();

            m_gameInput.Gameplay.Inventory.performed     += _ => OnInventory?.Invoke();
            m_gameInput.Gameplay.CharacterMenu.performed += _ => OnCharacterMenu?.Invoke();
            m_gameInput.Gameplay.Menu.performed          += _ => OnMenu?.Invoke();

            // 시작은 Gameplay 액션맵부터
            m_gameInput.Gameplay.Enable();
        }

        private void onMouseClickStarted(InputAction.CallbackContext ctx)
        {
            MouseClickedDown?.Invoke();
        }

        private void onMouseClickCanceled(InputAction.CallbackContext ctx)
        {
            MouseClickedUp?.Invoke();
        }

        // ─── 액션맵 전환 ───────────────────────────────────────────────
        // 채팅창이 열렸을 때 SwitchToUI() 호출 → Q/W/E/R 등 게임 키가 다 막힘
        // 채팅창이 닫히면 SwitchToGameplay() 호출

        public void SwitchToGameplay()
        {
            if (m_gameInput == null) return;
            m_gameInput.UI.Disable();
            m_gameInput.Gameplay.Enable();
        }

        public void SwitchToUI()
        {
            if (m_gameInput == null) return;
            m_gameInput.Gameplay.Disable();
            m_gameInput.UI.Enable();
        }

        // 씬 전환 시 등 구독자 일괄 해제용.
        // 캐릭터/UI 가 파괴되며 핸들러를 떼지 않은 경우에 대비.
        public void ClearSubscribers()
        {
            MouseClickedDown = null;
            MouseClickedUp = null;
            OnSkill1 = null;
            OnSkill2 = null;
            OnSkill3 = null;
            OnSkill4 = null;
            OnInventory = null;
            OnCharacterMenu = null;
            OnMenu = null;
        }

        public void Dispose()
        {
            if (m_gameInput == null) return;

            ClearSubscribers();
            m_gameInput.Disable();
            m_gameInput.Dispose();
            m_gameInput = null;
        }
    }
}
