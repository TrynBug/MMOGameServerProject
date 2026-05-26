using UnityEngine;

namespace Client.Managers
{
    // 모든 매니저의 진입점.
    //
    // Rookiss 스타일 싱글톤이지만 한 가지 차이:
    //   - Rookiss: GameObject.Find("@Managers") 로 자가복구
    //   - 우리: GameBootstrap 이 명시적으로 생성해 주입
    // 명시적 주입이 의도가 분명하고, 테스트/순서 제어가 쉬움.
    //
    // 사용법:
    //   Managers.Input.OnSkill1 += MySkillHandler;
    //   Managers.UI.ShowPopupUI<UI_MyPopup>();
    //   Managers.Resource.Load<GameObject>("UI/Popup/UI_Confirm");
    //   Managers.Scene.LoadScene(SceneManagerEx.SceneNames.Game);
    public class Managers : MonoBehaviour
    {
        private static Managers s_instance;
        public static Managers Instance => s_instance;

        // ─── Core 매니저들 ─────────────────────────────────────────────
        // 각각의 매니저는 순수 C# 클래스 (MonoBehaviour 아님).
        // 코루틴이나 Update가 필요한 매니저만 따로 MonoBehaviour로 만들면 됨.

        private InputManager m_input;
        public static InputManager Input => s_instance != null ? s_instance.m_input : null;

        private UIManager m_ui;
        public static UIManager UI => s_instance != null ? s_instance.m_ui : null;

        private ResourceManager m_resource;
        public static ResourceManager Resource => s_instance != null ? s_instance.m_resource : null;

        private SceneManagerEx m_scene;
        public static SceneManagerEx Scene => s_instance != null ? s_instance.m_scene : null;

        // ─── 초기화 / 정리 ─────────────────────────────────────────────

        // GameBootstrap 에서 호출. 한 번만 호출되어야 함.
        public static Managers CreateInstance()
        {
            if (s_instance != null)
            {
                Debug.LogWarning("[Managers] CreateInstance 가 중복 호출됨. 기존 인스턴스를 반환합니다.");
                return s_instance;
            }

            GameObject go = new GameObject("@Managers");
            DontDestroyOnLoad(go);

            s_instance = go.AddComponent<Managers>();
            s_instance.initManagers();

            return s_instance;
        }

        private void initManagers()
        {
            // ResourceManager 는 다른 매니저보다 먼저 만들어야 함.
            // 다른 매니저가 자산 로드를 필요로 할 수 있으니 가장 먼저.
            m_resource = new ResourceManager();

            m_input = new InputManager();
            m_input.Init();

            m_ui = new UIManager();
            // UIManager 는 별도 Init() 없음. 처음 Show*UI 호출 시 lazy 초기화.

            m_scene = new SceneManagerEx();
            // SceneManagerEx 는 별도 Init() 없음. 무상태.
        }

        // 씬 전환 시 매니저 상태를 초기화하고 싶을 때 사용.
        // SceneManagerEx.LoadScene 이 호출.
        public static void ClearForSceneTransition()
        {
            if (s_instance == null) return;
            s_instance.m_input?.ClearSubscribers();
            s_instance.m_ui?.Clear();
            // Resource, Scene 자체는 정리할 상태가 없음.
        }

        private void OnDestroy()
        {
            m_input?.Dispose();
            if (s_instance == this) s_instance = null;
        }
    }
}
