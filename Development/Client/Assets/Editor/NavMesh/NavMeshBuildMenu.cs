using System.IO;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

namespace MMO.Client.Navigation.Editor
{
    /// <summary>
    /// NavMesh 빌드의 진입점들을 모은 정적 클래스.
    ///
    /// 책임 분리:
    /// - NavMeshBuilder: 실제 빌드 로직 (씬 메시 수집 -> DotRecast 빌드 -> 파일 저장)
    /// - NavMeshBuildMenu: 사용자/외부 호출자가 빌드를 트리거하는 진입점
    ///
    /// 진입점 3가지:
    /// 1. Unity 메뉴 (Tools/NavMesh/Build Active Scene) -> 사람이 직접 클릭
    /// 2. AIEditorBridge 의 BUILD_NAVMESH 명령 -> 외부에서 ai_command.txt 통해 호출
    /// 3. 다른 에디터 스크립트가 직접 BuildScene() 호출
    ///
    /// 모두 같은 내부 함수(NavMeshBuilder.Build)를 호출하므로 동작이 일관된다.
    /// </summary>
    public static class NavMeshBuildMenu
    {
        // AssetDatabase.FindAssets 의 필터 문자열.
        // "t:TypeName" 형식이 표준이며, 해당 타입의 ScriptableObject 에셋만 검색.
        private const string SettingsAssetSearchFilter = "t:NavMeshBuildSettings";

        // ---------------------------------------------------------------------
        // 진입점 1: Unity 메뉴 클릭
        // ---------------------------------------------------------------------
        // [MenuItem] 어트리뷰트:
        // - Unity 의 상단 메뉴바에 항목을 추가.
        // - "/" 가 서브메뉴 구분자. "Tools/NavMesh/..." -> Tools 메뉴 안 NavMesh 서브메뉴.
        // - priority 는 메뉴 내 정렬 순서. 가까운 값끼리 그룹화되고 100 단위로 구분자 그어짐.
        // ---------------------------------------------------------------------
        [MenuItem("Tools/NavMesh/Build Active Scene", priority = 100)]
        public static void BuildActiveScene()
        {
            // 빌드 설정이 없으면 친절한 안내 다이얼로그 후 종료
            var settings = FindSettings();
            if (settings == null)
            {
                EditorUtility.DisplayDialog(
                    "NavMesh Build",
                    "NavMeshBuildSettings 에셋을 찾을 수 없습니다.\n" +
                    "프로젝트 어딘가에 Create > MMO > Navigation > NavMesh Build Settings 로 생성하세요.",
                    "OK");
                return;
            }

            // 활성 씬 이름을 스테이지 이름으로 사용 (Untitled 씬이면 비어있음)
            var scene = EditorSceneManager.GetActiveScene();
            string stageName = scene.name;
            if (string.IsNullOrEmpty(stageName))
            {
                EditorUtility.DisplayDialog("NavMesh Build", "활성 씬이 저장되지 않았습니다. 먼저 씬을 저장해주세요.", "OK");
                return;
            }

            // 빌드는 단일 함수 호출이지만 큰 맵에서는 수 초~수십 초가 걸릴 수 있어
            // 사용자에게 진행 중 표시. (실제 진행률 콜백은 받지 않으므로 단순 표시용)
            EditorUtility.DisplayProgressBar("NavMesh Build", $"빌드 중: {stageName}", 0.5f);
            try
            {
                var result = NavMeshBuilder.Build(settings, stageName);
                EditorUtility.ClearProgressBar();

                if (result.success)
                {
                    // 콘솔에도 남기고 다이얼로그도 띄움 (로그는 나중에 다시 볼 수 있어서)
                    string msg = result.message
                                 + "\n\nClient: " + result.clientPath
                                 + (result.serverPath != null ? "\nServer: " + result.serverPath : "");
                    Debug.Log("[NavMesh] " + msg);
                    EditorUtility.DisplayDialog("NavMesh Build 성공", msg, "OK");
                }
                else
                {
                    Debug.LogError("[NavMesh] 빌드 실패: " + result.message);
                    EditorUtility.DisplayDialog("NavMesh Build 실패", result.message, "OK");
                }
            }
            catch (System.Exception ex)
            {
                // 빌드 중 예외가 나도 progress bar 는 반드시 클리어
                EditorUtility.ClearProgressBar();
                Debug.LogException(ex);
                EditorUtility.DisplayDialog("NavMesh Build 예외", ex.Message, "OK");
            }
        }

        // ---------------------------------------------------------------------
        // 보조 메뉴: 설정 에셋 빠르게 찾기
        // ---------------------------------------------------------------------
        // 빌드 도구를 자주 쓰다 보면 NavMeshBuildSettings 에셋을 자주 찾게 된다.
        // Project 창에서 직접 클릭해도 되지만 메뉴로 한 번에 선택 가능하게 편의 제공.
        // ---------------------------------------------------------------------
        [MenuItem("Tools/NavMesh/Select Build Settings", priority = 101)]
        public static void SelectBuildSettings()
        {
            var settings = FindSettings();
            if (settings == null)
            {
                EditorUtility.DisplayDialog("NavMesh", "NavMeshBuildSettings 에셋이 없습니다. 먼저 생성하세요.", "OK");
                return;
            }
            // Selection.activeObject 로 인스펙터에 표시
            Selection.activeObject = settings;
            // PingObject 로 Project 창에서 해당 에셋을 깜빡이게 함 (위치 파악용)
            EditorGUIUtility.PingObject(settings);
        }

        /// <summary>
        /// 프로젝트에서 NavMeshBuildSettings 에셋을 찾아 반환한다 (첫 번째 발견된 것).
        ///
        /// AssetDatabase.FindAssets:
        /// - Unity 의 표준 에셋 검색 API. 백그라운드 인덱스를 사용해 빠르다.
        /// - GUID 만 반환하므로 GUIDToAssetPath 로 경로 변환 후 LoadAssetAtPath 로 실제 로드.
        ///
        /// 여러 개 있어도 첫 번째만 사용한다. 일반적으로 프로젝트당 1개면 충분.
        /// 만약 환경별로 다른 설정이 필요해지면 SerializedField 등으로 명시 참조하는 식으로 확장 가능.
        /// </summary>
        public static NavMeshBuildSettings FindSettings()
        {
            string[] guids = AssetDatabase.FindAssets(SettingsAssetSearchFilter);
            if (guids == null || guids.Length == 0)
                return null;

            string path = AssetDatabase.GUIDToAssetPath(guids[0]);
            return AssetDatabase.LoadAssetAtPath<NavMeshBuildSettings>(path);
        }

        // ---------------------------------------------------------------------
        // 진입점 2/3: 외부 호출용 진입점
        // ---------------------------------------------------------------------
        // BuildActiveScene 은 다이얼로그/프로그레스바 등 UI 부수효과가 있어서
        // 자동화(AIEditorBridge) 나 다른 스크립트가 호출하기 부적합하다.
        // BuildScene 은 순수하게 빌드만 하고 결과를 반환하는 진입점.
        //
        // AIEditorBridge 는 어셈블리 의존을 피하려고 리플렉션으로 이 메서드를 호출한다.
        // 그래서 시그니처(이름/파라미터/반환 타입)를 함부로 바꾸면 BUILD_NAVMESH 가 깨진다.
        // ---------------------------------------------------------------------
        public static NavMeshBuilder.BuildResult BuildScene(string stageName)
        {
            var settings = FindSettings();
            if (settings == null)
            {
                return new NavMeshBuilder.BuildResult
                {
                    success = false,
                    message = "NavMeshBuildSettings 에셋을 찾을 수 없습니다."
                };
            }

            // stageName 이 비어있으면 활성 씬 이름으로 자동 결정 (편의)
            if (string.IsNullOrEmpty(stageName))
                stageName = EditorSceneManager.GetActiveScene().name;

            return NavMeshBuilder.Build(settings, stageName);
        }
    }
}
