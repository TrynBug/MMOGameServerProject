using System.Collections.Generic;
using Client.UI;
using UnityEngine;

namespace Client.Managers
{
    // UI 의 생성/표시/닫기 흐름을 관리.
    //
    // 책임:
    //   - Scene UI / Popup UI / SubItem UI / WorldSpace UI 생성
    //   - Popup Stack 관리 (가장 위 popup 부터 닫힘)
    //   - Canvas 의 sorting order 자동 설정 (popup 이 누적되면 점점 위로)
    //   - UI 의 부모 GameObject "@UI_Root" 를 자동 생성/관리
    //
    // 책임이 아닌 것:
    //   - 개별 UI 의 데이터 갱신 (각 UI 자체가 함)
    //   - 패킷 처리 (Game 어셈블리 책임)
    //   - 프리팹 로드 / 파괴 (ResourceManager 책임)
    //
    // 프리팹 위치: Assets/Resources/UI/{Scene|Popup|SubItem|WorldSpace}/
    public class UIManager
    {
        // 다음에 생성될 popup 에 부여할 sorting order. 누적될수록 위에 표시.
        private int m_order = 10;

        // popup 의 생성 역순으로 닫기 위한 스택.
        private readonly Stack<UI_Popup> m_popupStack = new Stack<UI_Popup>();

        // 현재 떠있는 scene UI (없으면 null).
        private UI_Scene m_sceneUI;

        // ─── UI 부모 GameObject ─────────────────────────────────────────

        // Hierarchy 에서 모든 UI 의 부모가 되는 "@UI_Root" GameObject.
        // 없으면 자동 생성.
        private GameObject Root
        {
            get
            {
                GameObject root = GameObject.Find("@UI_Root");
                if (root == null)
                    root = new GameObject { name = "@UI_Root" };
                return root;
            }
        }

        // ─── Canvas 세팅 ───────────────────────────────────────────────

        // UI GameObject 에 Canvas 가 없으면 추가하고, sorting order 를 설정.
        // sort=true 이면 m_order 를 부여하고 증가, false 이면 0 (scene UI 용).
        public void SetCanvas(GameObject go, bool sort)
        {
            Canvas canvas = go.GetComponent<Canvas>();
            if (canvas == null) canvas = go.AddComponent<Canvas>();

            canvas.renderMode = RenderMode.ScreenSpaceOverlay;

            // 부모에 Canvas 가 있어도 자기만의 sorting order 를 적용하도록.
            // (popup 안에 또 popup 이 들어가는 등의 중첩 상황 대비)
            canvas.overrideSorting = true;

            if (sort)
            {
                canvas.sortingOrder = m_order;
                m_order++;
            }
            else
            {
                canvas.sortingOrder = 0;
            }
        }

        // ─── Show / Make ───────────────────────────────────────────────

        // Scene UI 생성. 프리팹 경로: Resources/UI/Scene/{name}
        // name 생략 시 T 의 클래스 이름으로 자동 결정.
        public T ShowSceneUI<T>(string name = null) where T : UI_Scene
        {
            if (string.IsNullOrEmpty(name)) name = typeof(T).Name;

            ensureEventSystem();   // UI 클릭 처리를 위한 EventSystem 보장 (Game 씬처럼 없는 경우 생성)

            GameObject go = Managers.Resource.Instantiate($"UI/Scene/{name}");
            if (go == null) return null;

            T sceneUI = go.GetComponent<T>();
            if (sceneUI == null) sceneUI = go.AddComponent<T>();

            m_sceneUI = sceneUI;
            go.transform.SetParent(Root.transform, worldPositionStays: false);

            SetCanvas(go, sort: false);
            sceneUI.Init();

            return sceneUI;
        }

        // Popup UI 생성. 프리팹 경로: Resources/UI/Popup/{name}
        // 생성 순서대로 stack 에 push 되어, ClosePopupUI() 시 가장 마지막 것이 먼저 닫힘.
        public T ShowPopupUI<T>(string name = null) where T : UI_Popup
        {
            if (string.IsNullOrEmpty(name)) name = typeof(T).Name;

            ensureEventSystem();

            GameObject go = Managers.Resource.Instantiate($"UI/Popup/{name}");
            if (go == null) return null;

            T popup = go.GetComponent<T>();
            if (popup == null) popup = go.AddComponent<T>();

            m_popupStack.Push(popup);
            go.transform.SetParent(Root.transform, worldPositionStays: false);

            SetCanvas(go, sort: true);
            popup.Init();

            return popup;
        }

        // 씬 UI(m_sceneUI) 와 별개로, 항상 떠있는 보조 오버레이 UI 를 생성한다. (예: 우상단 설정 버튼)
        // 프리팹 경로: Resources/UI/Scene/{name}
        //
        // ShowSceneUI 와 달리 m_sceneUI 슬롯을 차지하지 않으므로, 이미 씬 UI 가 떠 있어도 함께 놓을 수 있다.
        // sorting order 는 프리팹에 구운 Canvas 값을 그대로 쓴다(SetCanvas 미호출) — 씬 UI(0) 위, popup(10+) 아래로 세팅해 둘 것.
        // 씬 전환 시 @UI_Root 가 씬과 함께 파괴되므로 이 오버레이도 자동 정리된다.
        public T ShowOverlayUI<T>(string name = null) where T : UI_Base
        {
            if (string.IsNullOrEmpty(name)) name = typeof(T).Name;

            ensureEventSystem();

            GameObject go = Managers.Resource.Instantiate($"UI/Scene/{name}");
            if (go == null) return null;

            T ui = go.GetComponent<T>();
            if (ui == null) ui = go.AddComponent<T>();

            go.transform.SetParent(Root.transform, worldPositionStays: false);
            ui.Init();

            return ui;
        }

        // UI 안에 들어가는 subitem (예: 인벤토리의 슬롯 1개) 생성.
        // 프리팹 경로: Resources/UI/SubItem/{name}
        // parent 가 지정되면 그 transform 아래로, 아니면 떠있는 상태.
        public T MakeSubItem<T>(Transform parent = null, string name = null) where T : UI_Base
        {
            if (string.IsNullOrEmpty(name)) name = typeof(T).Name;

            GameObject go = Managers.Resource.Instantiate($"UI/SubItem/{name}");
            if (go == null) return null;

            if (parent != null) go.transform.SetParent(parent, worldPositionStays: false);

            T comp = go.GetComponent<T>();
            if (comp == null) comp = go.AddComponent<T>();

            return comp;
        }

        // World Space 에 떠있는 UI (예: 캐릭터 머리 위 HP바, 이름표) 생성.
        // 프리팹 경로: Resources/UI/WorldSpace/{name}
        public T MakeWorldSpaceUI<T>(Transform parent = null, string name = null) where T : UI_Base
        {
            if (string.IsNullOrEmpty(name)) name = typeof(T).Name;

            GameObject go = Managers.Resource.Instantiate($"UI/WorldSpace/{name}");
            if (go == null) return null;

            if (parent != null) go.transform.SetParent(parent, worldPositionStays: false);

            Canvas canvas = go.GetComponent<Canvas>();
            if (canvas == null) canvas = go.AddComponent<Canvas>();
            canvas.renderMode = RenderMode.WorldSpace;
            canvas.worldCamera = Camera.main;

            T comp = go.GetComponent<T>();
            if (comp == null) comp = go.AddComponent<T>();

            return comp;
        }

        // ─── Close ────────────────────────────────────────────────────

        // 인자로 지정된 popup 이 현재 스택의 top 일 때만 닫음.
        // 중간 popup 을 임의로 닫지 못하게 하기 위함 (스택 무결성 유지).
        public void ClosePopupUI(UI_Popup popup)
        {
            if (m_popupStack.Count == 0) return;

            if (m_popupStack.Peek() != popup)
            {
                Debug.LogWarning("[UIManager] ClosePopupUI: 지정 popup 이 스택 top 이 아님. 무시.");
                return;
            }

            ClosePopupUI();
        }

        // 가장 위 popup 을 닫는다.
        public void ClosePopupUI()
        {
            if (m_popupStack.Count == 0) return;

            UI_Popup popup = m_popupStack.Pop();
            Managers.Resource.Destroy(popup.gameObject);

            m_order--;
        }

        // 모든 popup 을 닫는다.
        public void CloseAllPopupUI()
        {
            while (m_popupStack.Count > 0)
                ClosePopupUI();
        }

        // UI 클릭(EventSystem) 보장. 씬에 EventSystem 이 없으면 생성한다.
        // 게임플레이만 있는 Game 씬은 EventSystem 이 없으므로, 거기서 팝업/HUD 버튼이 동작하려면 필요.
        private void ensureEventSystem()
        {
            if (UnityEngine.EventSystems.EventSystem.current != null) return;
            if (Object.FindAnyObjectByType<UnityEngine.EventSystems.EventSystem>() != null) return;

            GameObject go = new GameObject("@EventSystem");
            go.AddComponent<UnityEngine.EventSystems.EventSystem>();
            go.AddComponent<UnityEngine.InputSystem.UI.InputSystemUIInputModule>();   // 기본 UI 액션 자동 할당됨
        }

        // 씬 전환 등 일괄 정리 시 호출.
        public void Clear()
        {
            CloseAllPopupUI();
            m_sceneUI = null;
            m_order = 10;
        }
    }
}
