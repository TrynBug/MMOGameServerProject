using UnityEngine;
using UnitySceneManager = UnityEngine.SceneManagement.SceneManager;

namespace Client.Managers
{
    // 씬 전환을 관리. UnityEngine.SceneManagement.SceneManager 의 래퍼.
    //
    // 책임:
    //   - 씬 전환 직전에 매니저 상태 정리 (UI clear, Input 구독자 정리 등)
    //   - 현재 씬의 BaseScene.Clear() 호출
    //   - 매직 스트링 방지용 SceneNames 상수 제공
    //
    // 책임이 아닌 것:
    //   - 씬 로드 후의 작업 (각 씬의 BaseScene.Init() 이 알아서 함)
    //   - 씬 간 데이터 전달 (DontDestroyOnLoad 객체나 별도 캐시로 처리)
    public class SceneManagerEx
    {
        // 매직 스트링 방지를 위한 씬 이름 상수.
        // Unity 의 Build Settings 에 등록된 씬 이름과 일치해야 함.
        public static class SceneNames
        {
            public const string Login = "Login";
            public const string Game = "Game";
            // 추후 CharacterSelect 등 추가 가능
        }

        // 현재 활성 씬의 BaseScene 컴포넌트.
        // 씬 안에 BaseScene 상속 컴포넌트가 부착된 GameObject 가 있어야 함.
        // 없으면 null (씬 전환 시 정리할 게 없는 것으로 간주).
        public BaseScene CurrentScene => Object.FindFirstObjectByType<BaseScene>();

        // 씬 전환.
        //   1) Managers 의 일괄 정리 (UI close, Input 구독자 정리)
        //   2) 현재 씬의 BaseScene.Clear() 호출 (씬별 정리 로직)
        //   3) Unity 씬 로드
        //
        // 호출 예: Managers.Scene.LoadScene(SceneManagerEx.SceneNames.Game);
        public void LoadScene(string sceneName)
        {
            // (1) 매니저 일괄 정리
            Managers.ClearForSceneTransition();

            // (2) 현재 씬의 정리 작업
            CurrentScene?.Clear();

            // (3) Unity 씬 로드
            UnitySceneManager.LoadScene(sceneName);
        }
    }
}
