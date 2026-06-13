using UnityEngine;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEditor.Compilation;
using UnityEditor.TestTools.TestRunner.Api;
using UnityEngine.SceneManagement;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;

[InitializeOnLoad]
public class AIEditorBridge
{
    private const string TriggerPath = "Assets/Editor/ai_command.txt";
    private const string ResultPath = "Assets/Editor/ai_command_result.txt";
    private const string PendingCompileFlag = "AIEditorBridge_PendingCompile";

    static AIEditorBridge()
    {
        EditorApplication.update += CheckAICommand;

        // 컴파일 완료 이벤트: COMPILE_CHECK 결과 출력에 사용
        CompilationPipeline.compilationFinished += OnCompilationFinished;
    }

    // =========================================================================
    // 명령 감지 및 디스패치
    // =========================================================================

    private static void CheckAICommand()
    {
        if (!File.Exists(TriggerPath))
            return;

        string command = File.ReadAllText(TriggerPath).Trim();
        File.Delete(TriggerPath);

        if (string.IsNullOrEmpty(command))
            return;

        Debug.Log($"[AI MCP] 명령 수신: {command}");

        try
        {
            ExecuteCommand(command);
        }
        catch (Exception ex)
        {
            WriteResult(false, $"명령 실행 중 예외 발생: {ex.GetType().Name}: {ex.Message}\n{ex.StackTrace}");
        }
    }

    private static void ExecuteCommand(string command)
    {
        string[] parts = command.Split(':');
        string cmd = parts[0].Trim();
        string[] args = parts.Skip(1).ToArray();

        switch (cmd)
        {
            // 기존 명령
            case "PLAY":
                EditorApplication.isPlaying = true;
                WriteResult(true, "플레이 모드 시작");
                break;

            case "STOP":
                EditorApplication.isPlaying = false;
                WriteResult(true, "플레이 모드 종료");
                break;

            case "REFRESH":
                AssetDatabase.Refresh();
                WriteResult(true, "AssetDatabase Refresh 완료");
                break;

            // 컴파일/테스트
            case "COMPILE_CHECK":
                HandleCompileCheck();
                break;

            case "RUN_EDIT_MODE_TESTS":
                HandleRunTests(null);
                break;

            case "RUN_EDIT_MODE_TEST":
                HandleRunTests(GetArg(args, 0));
                break;

            // 씬 조작
            case "SCENE_DUMP":
                if (args.Length == 0)
                    HandleSceneDumpActive();
                else
                    HandleSceneDumpFile(GetArg(args, 0));
                break;

            case "CREATE_SCENE":
                HandleCreateScene(GetArg(args, 0));
                break;

            case "OPEN_SCENE":
                HandleOpenScene(GetArg(args, 0));
                break;

            case "SAVE_SCENE":
                HandleSaveScene();
                break;

            // 게임오브젝트 조작
            case "CREATE_PRIMITIVE":
                HandleCreatePrimitive(GetArg(args, 0), GetArg(args, 1), GetArg(args, 2));
                break;

            case "CREATE_EMPTY":
                HandleCreateEmpty(GetArg(args, 0), GetArg(args, 1));
                break;

            case "ADD_COMPONENT":
                HandleAddComponent(GetArg(args, 0), GetArg(args, 1));
                break;

            case "DELETE_OBJECT":
                HandleDeleteObject(GetArg(args, 0));
                break;

            case "SET_POSITION":
                HandleSetPosition(GetArg(args, 0), GetArg(args, 1));
                break;

            // 프리팹
            case "CREATE_PREFAB":
                HandleCreatePrefab(GetArg(args, 0), GetArg(args, 1));
                break;

            case "INSTANTIATE_PREFAB":
                HandleInstantiatePrefab(GetArg(args, 0), GetArg(args, 1), GetArg(args, 2));
                break;

            // NavMesh
            case "BUILD_NAVMESH":
                HandleBuildNavMesh(GetArg(args, 0));
                break;

            // StageLayout export (Spawner/SpawnPoint/EventArea/Waypoint 마커 -> 서버 json)
            case "EXPORT_STAGELAYOUT":
                HandleExportStageLayout(GetArg(args, 0));
                break;

            // 콘솔/정보
            case "GET_CONSOLE_LOGS":
                HandleGetConsoleLogs();
                break;

            case "CLEAR_CONSOLE":
                HandleClearConsole();
                break;

            case "PROJECT_INFO":
                HandleProjectInfo();
                break;

            default:
                WriteResult(false, $"알 수 없는 AI 명령: {cmd}");
                Debug.LogWarning($"알 수 없는 AI 명령: {cmd}");
                break;
        }
    }

    private static string GetArg(string[] args, int index)
    {
        if (args == null || index >= args.Length)
            return null;
        return args[index];
    }

    // =========================================================================
    // 결과 출력 유틸
    // =========================================================================

    private static void WriteResult(bool success, string message)
    {
        string header = success ? "OK" : "ERROR";
        string content = $"{header}\n{message ?? ""}";
        File.WriteAllText(ResultPath, content);
        AssetDatabase.Refresh();
    }

    // =========================================================================
    // 컴파일 체크
    // =========================================================================

    private static void HandleCompileCheck()
    {
        // 컴파일 완료 콜백에서 결과를 쓰도록 플래그 세팅
        SessionState.SetBool(PendingCompileFlag, true);
        CompilationPipeline.RequestScriptCompilation();
        // 결과는 OnCompilationFinished 에서 작성됨
    }

    private static void OnCompilationFinished(object obj)
    {
        if (!SessionState.GetBool(PendingCompileFlag, false))
            return;

        SessionState.EraseBool(PendingCompileFlag);

        var assemblies = CompilationPipeline.GetAssemblies(AssembliesType.Editor);
        var sb = new StringBuilder();
        int errorCount = 0;
        int warningCount = 0;

        // CompilationPipeline.assemblyCompilationFinished 가 없으니
        // 콘솔 로그에서 에러를 긁어옴
        var logs = GetLogEntries();
        foreach (var log in logs)
        {
            if (log.mode.HasFlag(LogMode.Error) || log.mode.HasFlag(LogMode.ScriptingError) || log.mode.HasFlag(LogMode.ScriptCompileError))
            {
                errorCount++;
                sb.AppendLine($"[ERROR] {log.file}({log.line}): {log.message}");
            }
            else if (log.mode.HasFlag(LogMode.ScriptingWarning) || log.mode.HasFlag(LogMode.ScriptCompileWarning))
            {
                warningCount++;
                sb.AppendLine($"[WARN]  {log.file}({log.line}): {log.message}");
            }
        }

        bool success = errorCount == 0;
        string summary = $"컴파일 완료. 에러: {errorCount}, 경고: {warningCount}\n";
        WriteResult(success, summary + sb.ToString());
    }

    // =========================================================================
    // 테스트 실행
    // =========================================================================

    private static void HandleRunTests(string testName)
    {
        var api = ScriptableObject.CreateInstance<TestRunnerApi>();
        var filter = new Filter { testMode = TestMode.EditMode };
        if (!string.IsNullOrEmpty(testName))
            filter.testNames = new[] { testName };

        var callback = new TestCallbacks();
        api.RegisterCallbacks(callback);
        api.Execute(new ExecutionSettings(filter));
        // 결과는 TestCallbacks.RunFinished 에서 작성
    }

    private class TestCallbacks : ICallbacks
    {
        private readonly StringBuilder m_log = new StringBuilder();
        private int m_pass;
        private int m_fail;
        private int m_skip;

        public void RunStarted(ITestAdaptor testsToRun) { }

        public void RunFinished(ITestResultAdaptor result)
        {
            bool success = m_fail == 0;
            string summary = $"테스트 완료. 통과: {m_pass}, 실패: {m_fail}, 스킵: {m_skip}\n";
            WriteResult(success, summary + m_log.ToString());
        }

        public void TestStarted(ITestAdaptor test) { }

        public void TestFinished(ITestResultAdaptor result)
        {
            if (result.HasChildren) return;

            switch (result.TestStatus)
            {
                case TestStatus.Passed:
                    m_pass++;
                    break;
                case TestStatus.Failed:
                    m_fail++;
                    m_log.AppendLine($"[FAIL] {result.FullName}");
                    if (!string.IsNullOrEmpty(result.Message))
                        m_log.AppendLine($"       {result.Message}");
                    if (!string.IsNullOrEmpty(result.StackTrace))
                        m_log.AppendLine($"       {result.StackTrace}");
                    break;
                case TestStatus.Skipped:
                case TestStatus.Inconclusive:
                    m_skip++;
                    break;
            }
        }
    }

    // =========================================================================
    // 씬 조작
    // =========================================================================

    private static void HandleSceneDumpActive()
    {
        var scene = SceneManager.GetActiveScene();
        var sb = new StringBuilder();
        sb.AppendLine($"씬: {scene.name} ({scene.path})");
        sb.AppendLine($"루트 오브젝트 수: {scene.rootCount}");
        sb.AppendLine();

        foreach (var root in scene.GetRootGameObjects())
            DumpGameObject(root, 0, sb);

        WriteResult(true, sb.ToString());
    }

    private static void HandleSceneDumpFile(string scenePath)
    {
        if (string.IsNullOrEmpty(scenePath))
        {
            WriteResult(false, "SCENE_DUMP: scenePath 인자가 필요합니다.");
            return;
        }

        if (!File.Exists(scenePath))
        {
            WriteResult(false, $"SCENE_DUMP: 씬 파일을 찾을 수 없습니다: {scenePath}");
            return;
        }

        // 임시 로드 후 덤프, 원래 씬으로 복귀
        var previousScene = SceneManager.GetActiveScene().path;
        var loadedScene = EditorSceneManager.OpenScene(scenePath, OpenSceneMode.Additive);
        var sb = new StringBuilder();
        sb.AppendLine($"씬: {loadedScene.name} ({loadedScene.path})");
        sb.AppendLine($"루트 오브젝트 수: {loadedScene.rootCount}");
        sb.AppendLine();
        foreach (var root in loadedScene.GetRootGameObjects())
            DumpGameObject(root, 0, sb);

        EditorSceneManager.CloseScene(loadedScene, true);

        WriteResult(true, sb.ToString());
    }

    private static void DumpGameObject(GameObject go, int depth, StringBuilder sb)
    {
        string indent = new string(' ', depth * 2);
        var components = go.GetComponents<Component>()
            .Where(c => c != null)
            .Select(c => c.GetType().Name);
        var pos = go.transform.localPosition;
        sb.AppendLine($"{indent}- {go.name} [pos=({pos.x:F2},{pos.y:F2},{pos.z:F2})] components=[{string.Join(",", components)}]");

        for (int i = 0; i < go.transform.childCount; i++)
            DumpGameObject(go.transform.GetChild(i).gameObject, depth + 1, sb);
    }

    private static void HandleCreateScene(string scenePath)
    {
        if (string.IsNullOrEmpty(scenePath))
        {
            WriteResult(false, "CREATE_SCENE: scenePath 인자가 필요합니다.");
            return;
        }

        var scene = EditorSceneManager.NewScene(NewSceneSetup.EmptyScene, NewSceneMode.Single);
        bool saved = EditorSceneManager.SaveScene(scene, scenePath);
        if (!saved)
        {
            WriteResult(false, $"CREATE_SCENE: 씬 저장 실패: {scenePath}");
            return;
        }

        WriteResult(true, $"씬 생성 완료: {scenePath}");
    }

    private static void HandleOpenScene(string scenePath)
    {
        if (string.IsNullOrEmpty(scenePath))
        {
            WriteResult(false, "OPEN_SCENE: scenePath 인자가 필요합니다.");
            return;
        }

        if (!File.Exists(scenePath))
        {
            WriteResult(false, $"OPEN_SCENE: 씬 파일을 찾을 수 없습니다: {scenePath}");
            return;
        }

        var scene = EditorSceneManager.OpenScene(scenePath, OpenSceneMode.Single);
        WriteResult(true, $"씬 열기 완료: {scene.path}");
    }

    private static void HandleSaveScene()
    {
        var scene = SceneManager.GetActiveScene();
        bool saved = EditorSceneManager.SaveScene(scene);
        if (!saved)
        {
            WriteResult(false, $"SAVE_SCENE: 저장 실패: {scene.path}");
            return;
        }

        WriteResult(true, $"씬 저장 완료: {scene.path}");
    }

    // =========================================================================
    // 게임오브젝트 조작
    // =========================================================================

    private static readonly Dictionary<string, PrimitiveType> sm_primitiveMap = new Dictionary<string, PrimitiveType>(StringComparer.OrdinalIgnoreCase)
    {
        { "Cube", PrimitiveType.Cube },
        { "Sphere", PrimitiveType.Sphere },
        { "Capsule", PrimitiveType.Capsule },
        { "Cylinder", PrimitiveType.Cylinder },
        { "Plane", PrimitiveType.Plane },
        { "Quad", PrimitiveType.Quad },
    };

    private static void HandleCreatePrimitive(string type, string name, string posStr)
    {
        if (string.IsNullOrEmpty(type) || string.IsNullOrEmpty(name))
        {
            WriteResult(false, "CREATE_PRIMITIVE: type, name 인자가 필요합니다. 예: CREATE_PRIMITIVE:Cube:Player:0,0,0");
            return;
        }

        if (!sm_primitiveMap.TryGetValue(type, out var primitiveType))
        {
            WriteResult(false, $"CREATE_PRIMITIVE: 지원하지 않는 type: {type}. 지원: Cube,Sphere,Capsule,Cylinder,Plane,Quad");
            return;
        }

        var go = GameObject.CreatePrimitive(primitiveType);
        go.name = name;
        if (TryParseVector3(posStr, out var pos))
            go.transform.position = pos;

        Undo.RegisterCreatedObjectUndo(go, "Create Primitive");
        EditorSceneManager.MarkSceneDirty(go.scene);
        WriteResult(true, $"프리미티브 생성: {name} ({primitiveType}) at {go.transform.position}");
    }

    private static void HandleCreateEmpty(string name, string posStr)
    {
        if (string.IsNullOrEmpty(name))
        {
            WriteResult(false, "CREATE_EMPTY: name 인자가 필요합니다.");
            return;
        }

        var go = new GameObject(name);
        if (TryParseVector3(posStr, out var pos))
            go.transform.position = pos;

        Undo.RegisterCreatedObjectUndo(go, "Create Empty");
        EditorSceneManager.MarkSceneDirty(go.scene);
        WriteResult(true, $"빈 오브젝트 생성: {name} at {go.transform.position}");
    }

    private static void HandleAddComponent(string objectPath, string componentTypeName)
    {
        if (string.IsNullOrEmpty(objectPath) || string.IsNullOrEmpty(componentTypeName))
        {
            WriteResult(false, "ADD_COMPONENT: objectPath, componentTypeName 인자가 필요합니다.");
            return;
        }

        var go = FindGameObjectByPath(objectPath);
        if (go == null)
        {
            WriteResult(false, $"ADD_COMPONENT: 오브젝트를 찾을 수 없습니다: {objectPath}");
            return;
        }

        var type = FindTypeByName(componentTypeName);
        if (type == null)
        {
            WriteResult(false, $"ADD_COMPONENT: 컴포넌트 타입을 찾을 수 없습니다: {componentTypeName}");
            return;
        }

        if (!typeof(Component).IsAssignableFrom(type))
        {
            WriteResult(false, $"ADD_COMPONENT: {componentTypeName} 은 Component 가 아닙니다.");
            return;
        }

        var added = Undo.AddComponent(go, type);
        EditorSceneManager.MarkSceneDirty(go.scene);
        WriteResult(true, $"컴포넌트 추가: {go.name} <- {added.GetType().FullName}");
    }

    private static void HandleDeleteObject(string objectPath)
    {
        if (string.IsNullOrEmpty(objectPath))
        {
            WriteResult(false, "DELETE_OBJECT: objectPath 인자가 필요합니다.");
            return;
        }

        var go = FindGameObjectByPath(objectPath);
        if (go == null)
        {
            WriteResult(false, $"DELETE_OBJECT: 오브젝트를 찾을 수 없습니다: {objectPath}");
            return;
        }

        var scene = go.scene;
        Undo.DestroyObjectImmediate(go);
        EditorSceneManager.MarkSceneDirty(scene);
        WriteResult(true, $"오브젝트 삭제: {objectPath}");
    }

    private static void HandleSetPosition(string objectPath, string posStr)
    {
        if (string.IsNullOrEmpty(objectPath))
        {
            WriteResult(false, "SET_POSITION: objectPath 인자가 필요합니다.");
            return;
        }

        if (!TryParseVector3(posStr, out var pos))
        {
            WriteResult(false, $"SET_POSITION: 좌표 파싱 실패: {posStr}");
            return;
        }

        var go = FindGameObjectByPath(objectPath);
        if (go == null)
        {
            WriteResult(false, $"SET_POSITION: 오브젝트를 찾을 수 없습니다: {objectPath}");
            return;
        }

        Undo.RecordObject(go.transform, "Set Position");
        go.transform.position = pos;
        EditorSceneManager.MarkSceneDirty(go.scene);
        WriteResult(true, $"위치 설정: {objectPath} -> {pos}");
    }

    // =========================================================================
    // 프리팹
    // =========================================================================

    private static void HandleCreatePrefab(string objectPath, string prefabPath)
    {
        if (string.IsNullOrEmpty(objectPath) || string.IsNullOrEmpty(prefabPath))
        {
            WriteResult(false, "CREATE_PREFAB: objectPath, prefabPath 인자가 필요합니다.");
            return;
        }

        var go = FindGameObjectByPath(objectPath);
        if (go == null)
        {
            WriteResult(false, $"CREATE_PREFAB: 오브젝트를 찾을 수 없습니다: {objectPath}");
            return;
        }

        string dir = Path.GetDirectoryName(prefabPath);
        if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir))
            Directory.CreateDirectory(dir);

        var prefab = PrefabUtility.SaveAsPrefabAsset(go, prefabPath, out bool success);
        if (!success || prefab == null)
        {
            WriteResult(false, $"CREATE_PREFAB: 프리팹 저장 실패: {prefabPath}");
            return;
        }

        WriteResult(true, $"프리팹 생성: {prefabPath}");
    }

    private static void HandleInstantiatePrefab(string prefabPath, string name, string posStr)
    {
        if (string.IsNullOrEmpty(prefabPath))
        {
            WriteResult(false, "INSTANTIATE_PREFAB: prefabPath 인자가 필요합니다.");
            return;
        }

        var prefab = AssetDatabase.LoadAssetAtPath<GameObject>(prefabPath);
        if (prefab == null)
        {
            WriteResult(false, $"INSTANTIATE_PREFAB: 프리팹을 찾을 수 없습니다: {prefabPath}");
            return;
        }

        var instance = (GameObject)PrefabUtility.InstantiatePrefab(prefab);
        if (!string.IsNullOrEmpty(name))
            instance.name = name;
        if (TryParseVector3(posStr, out var pos))
            instance.transform.position = pos;

        Undo.RegisterCreatedObjectUndo(instance, "Instantiate Prefab");
        EditorSceneManager.MarkSceneDirty(instance.scene);
        WriteResult(true, $"프리팹 인스턴스화: {instance.name} from {prefabPath} at {instance.transform.position}");
    }

    // =========================================================================
    // 콘솔/정보
    // =========================================================================

    private static void HandleGetConsoleLogs()
    {
        var logs = GetLogEntries();
        var sb = new StringBuilder();
        sb.AppendLine($"총 로그: {logs.Count}");
        foreach (var log in logs)
        {
            string level = "INFO";
            if (log.mode.HasFlag(LogMode.Error) || log.mode.HasFlag(LogMode.ScriptingError) || log.mode.HasFlag(LogMode.ScriptCompileError))
                level = "ERROR";
            else if (log.mode.HasFlag(LogMode.ScriptingWarning) || log.mode.HasFlag(LogMode.ScriptCompileWarning))
                level = "WARN";

            sb.AppendLine($"[{level}] {log.file}({log.line}): {log.message}");
        }
        WriteResult(true, sb.ToString());
    }

    private static void HandleClearConsole()
    {
        var logEntries = Type.GetType("UnityEditor.LogEntries,UnityEditor.dll");
        if (logEntries == null)
        {
            WriteResult(false, "CLEAR_CONSOLE: UnityEditor.LogEntries 를 찾을 수 없습니다.");
            return;
        }
        var clearMethod = logEntries.GetMethod("Clear", BindingFlags.Static | BindingFlags.Public);
        clearMethod?.Invoke(null, null);
        WriteResult(true, "콘솔 클리어 완료");
    }

    // =========================================================================
    // NavMesh 빌드
    // =========================================================================

    private static void HandleBuildNavMesh(string stageName)
    {
        // 인자 없으면 활성 씬 이름 사용
        if (string.IsNullOrEmpty(stageName))
            stageName = SceneManager.GetActiveScene().name;

        if (string.IsNullOrEmpty(stageName))
        {
            WriteResult(false, "BUILD_NAVMESH: 활성 씬이 저장되지 않았거나 stageName 인자가 비어있습니다.");
            return;
        }

        // NavMeshBuildMenu.BuildScene 을 리플렉션으로 호출 (Editor 어셈블리 의존 회피)
        var menuType = Type.GetType("MMO.Client.Navigation.Editor.NavMeshBuildMenu, Assembly-CSharp-Editor");
        if (menuType == null)
        {
            // 다른 어셈블리에 있을 수도 있으니 전체 검색
            foreach (var asm in AppDomain.CurrentDomain.GetAssemblies())
            {
                menuType = asm.GetType("MMO.Client.Navigation.Editor.NavMeshBuildMenu");
                if (menuType != null) break;
            }
        }

        if (menuType == null)
        {
            WriteResult(false, "BUILD_NAVMESH: NavMeshBuildMenu 타입을 찾을 수 없습니다. 빌드 도구가 설치되어 있나요?");
            return;
        }

        var buildMethod = menuType.GetMethod("BuildScene", BindingFlags.Static | BindingFlags.Public);
        if (buildMethod == null)
        {
            WriteResult(false, "BUILD_NAVMESH: NavMeshBuildMenu.BuildScene 메서드를 찾을 수 없습니다.");
            return;
        }

        object resultObj = buildMethod.Invoke(null, new object[] { stageName });
        if (resultObj == null)
        {
            WriteResult(false, "BUILD_NAVMESH: BuildScene 이 null 을 반환했습니다.");
            return;
        }

        Type resultType = resultObj.GetType();
        bool success = (bool)(resultType.GetField("success")?.GetValue(resultObj) ?? false);
        string message = resultType.GetField("message")?.GetValue(resultObj) as string ?? "";
        string clientPath = resultType.GetField("clientPath")?.GetValue(resultObj) as string ?? "";
        string serverPath = resultType.GetField("serverPath")?.GetValue(resultObj) as string ?? "";

        var sb = new StringBuilder();
        sb.AppendLine(message);
        if (!string.IsNullOrEmpty(clientPath))
            sb.AppendLine($"Client: {clientPath}");
        if (!string.IsNullOrEmpty(serverPath))
            sb.AppendLine($"Server: {serverPath}");
        WriteResult(success, sb.ToString());
    }

    // StageLayoutExportMenu.ExportScene 을 리플렉션으로 호출 (BUILD_NAVMESH 와 동일 패턴).
    private static void HandleExportStageLayout(string sceneName)
    {
        if (string.IsNullOrEmpty(sceneName))
            sceneName = SceneManager.GetActiveScene().name;

        if (string.IsNullOrEmpty(sceneName))
        {
            WriteResult(false, "EXPORT_STAGELAYOUT: 활성 씬이 저장되지 않았거나 sceneName 인자가 비어있습니다.");
            return;
        }

        Type menuType = Type.GetType("Client.EditorTools.StageLayoutExportMenu, Assembly-CSharp-Editor");
        if (menuType == null)
        {
            foreach (var asm in AppDomain.CurrentDomain.GetAssemblies())
            {
                menuType = asm.GetType("Client.EditorTools.StageLayoutExportMenu");
                if (menuType != null) break;
            }
        }

        if (menuType == null)
        {
            WriteResult(false, "EXPORT_STAGELAYOUT: StageLayoutExportMenu 타입을 찾을 수 없습니다.");
            return;
        }

        var exportMethod = menuType.GetMethod("ExportScene", BindingFlags.Static | BindingFlags.Public);
        if (exportMethod == null)
        {
            WriteResult(false, "EXPORT_STAGELAYOUT: StageLayoutExportMenu.ExportScene 메서드를 찾을 수 없습니다.");
            return;
        }

        object resultObj = exportMethod.Invoke(null, new object[] { sceneName });
        if (resultObj == null)
        {
            WriteResult(false, "EXPORT_STAGELAYOUT: ExportScene 이 null 을 반환했습니다.");
            return;
        }

        Type resultType = resultObj.GetType();
        bool success = (bool)(resultType.GetField("success")?.GetValue(resultObj) ?? false);
        string message = resultType.GetField("message")?.GetValue(resultObj) as string ?? "";
        WriteResult(success, message);
    }

    private static void HandleProjectInfo()
    {
        var sb = new StringBuilder();
        sb.AppendLine($"Unity 버전: {Application.unityVersion}");
        sb.AppendLine($"프로젝트 경로: {Path.GetFullPath(Application.dataPath + "/..")}");
        sb.AppendLine($"활성 씬: {SceneManager.GetActiveScene().path}");
        sb.AppendLine($"플레이 모드: {EditorApplication.isPlaying}");
        sb.AppendLine($"컴파일 중: {EditorApplication.isCompiling}");
        WriteResult(true, sb.ToString());
    }

    // =========================================================================
    // 유틸: 파싱, 검색
    // =========================================================================

    private static bool TryParseVector3(string s, out Vector3 result)
    {
        result = Vector3.zero;
        if (string.IsNullOrEmpty(s))
            return false;

        var parts = s.Split(',');
        if (parts.Length != 3)
            return false;

        if (!float.TryParse(parts[0], System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out float x)) return false;
        if (!float.TryParse(parts[1], System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out float y)) return false;
        if (!float.TryParse(parts[2], System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out float z)) return false;

        result = new Vector3(x, y, z);
        return true;
    }

    // "Parent/Child/GrandChild" 경로로 게임오브젝트를 찾음
    private static GameObject FindGameObjectByPath(string path)
    {
        if (string.IsNullOrEmpty(path))
            return null;

        var scene = SceneManager.GetActiveScene();
        var parts = path.Split('/');
        foreach (var root in scene.GetRootGameObjects())
        {
            if (root.name != parts[0])
                continue;

            var current = root.transform;
            for (int i = 1; i < parts.Length; i++)
            {
                var next = current.Find(parts[i]);
                if (next == null) { current = null; break; }
                current = next;
            }
            if (current != null)
                return current.gameObject;
        }
        return null;
    }

    private static Type FindTypeByName(string typeName)
    {
        // 정확한 FullName 먼저
        var type = Type.GetType(typeName);
        if (type != null) return type;

        // 모든 로드된 어셈블리에서 검색
        foreach (var assembly in AppDomain.CurrentDomain.GetAssemblies())
        {
            type = assembly.GetType(typeName);
            if (type != null) return type;

            // 단순 이름 매칭
            type = assembly.GetTypes().FirstOrDefault(t => t.Name == typeName);
            if (type != null) return type;
        }
        return null;
    }

    // =========================================================================
    // 로그 엔트리 리플렉션 (UnityEditor.LogEntry 는 internal)
    // =========================================================================

    private struct LogEntryInfo
    {
        public string message;
        public string file;
        public int line;
        public LogMode mode;
    }

    [Flags]
    private enum LogMode
    {
        Error = 1 << 0,
        Assert = 1 << 1,
        Log = 1 << 2,
        Fatal = 1 << 4,
        DontPreprocessCondition = 1 << 5,
        AssetImportError = 1 << 6,
        AssetImportWarning = 1 << 7,
        ScriptingError = 1 << 8,
        ScriptingWarning = 1 << 9,
        ScriptingLog = 1 << 10,
        ScriptCompileError = 1 << 11,
        ScriptCompileWarning = 1 << 12,
        StickyError = 1 << 13,
        MayIgnoreLineNumber = 1 << 14,
        ReportBug = 1 << 15,
        DisplayPreviousErrorInStatusBar = 1 << 16,
        ScriptingException = 1 << 17,
        DontExtractStacktrace = 1 << 18,
        ShouldClearOnPlay = 1 << 19,
        GraphCompileError = 1 << 20,
        ScriptingAssertion = 1 << 21,
        VisualScriptingError = 1 << 22,
    }

    private static List<LogEntryInfo> GetLogEntries()
    {
        var result = new List<LogEntryInfo>();
        var logEntriesType = Type.GetType("UnityEditor.LogEntries,UnityEditor.dll");
        var logEntryType = Type.GetType("UnityEditor.LogEntry,UnityEditor.dll");
        if (logEntriesType == null || logEntryType == null)
            return result;

        var startGettingEntries = logEntriesType.GetMethod("StartGettingEntries", BindingFlags.Static | BindingFlags.Public);
        var endGettingEntries = logEntriesType.GetMethod("EndGettingEntries", BindingFlags.Static | BindingFlags.Public);
        var getEntryInternal = logEntriesType.GetMethod("GetEntryInternal", BindingFlags.Static | BindingFlags.Public);

        if (startGettingEntries == null || endGettingEntries == null || getEntryInternal == null)
            return result;

        int count = (int)startGettingEntries.Invoke(null, null);
        try
        {
            var logEntry = Activator.CreateInstance(logEntryType);
            var messageField = logEntryType.GetField("message");
            var fileField = logEntryType.GetField("file");
            var lineField = logEntryType.GetField("line");
            var modeField = logEntryType.GetField("mode");

            for (int i = 0; i < count; i++)
            {
                getEntryInternal.Invoke(null, new object[] { i, logEntry });
                var info = new LogEntryInfo
                {
                    message = messageField?.GetValue(logEntry) as string,
                    file = fileField?.GetValue(logEntry) as string,
                    line = lineField != null ? (int)lineField.GetValue(logEntry) : 0,
                    mode = modeField != null ? (LogMode)(int)modeField.GetValue(logEntry) : 0,
                };
                result.Add(info);
            }
        }
        finally
        {
            endGettingEntries.Invoke(null, null);
        }
        return result;
    }
}
