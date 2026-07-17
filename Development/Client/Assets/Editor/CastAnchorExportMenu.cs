using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using Client.Game;
using UnityEditor;
using UnityEngine;

namespace Client.EditorTools
{
    /// <summary>
    /// 캐릭터/몬스터 프리팹 Root 직속 SkillCastOrigin을 서버용 Map/CastAnchors.json으로 내보냅니다.
    /// GameDataGenerator가 소유하는 xlsx/csv를 수정하지 않으며, 프리팹이 발사점의 원본입니다.
    /// </summary>
    public static class CastAnchorExportMenu
    {
        private const string ServerOutputDir = "../../Server/OUTPUT/Map";
        private const string OutputFileName = "CastAnchors.json";
        private const string FolderPrefsKey = "Client.CastAnchors.PrefabFolders";
        private static readonly string[] DefaultPrefabFolders =
        {
            "Assets/Resources/Prefabs/Characters",
            "Assets/Resources/Prefabs/Monsters",
        };
        private enum AnchorReadResult
        {
            Valid,
            UseDefault,
            Error,
        }

        private readonly struct AnchorEntry
        {
            public readonly int keyA;
            public readonly int keyB;
            public readonly Vector3 localOffset;

            public AnchorEntry(int keyA, int keyB, Vector3 localOffset)
            {
                this.keyA = keyA;
                this.keyB = keyB;
                this.localOffset = localOffset;
            }
        }

        private readonly struct PrefabAnchorEntry
        {
            public readonly string prefabPath;
            public readonly Vector3 localOffset;

            public PrefabAnchorEntry(string prefabPath, Vector3 localOffset)
            {
                this.prefabPath = prefabPath;
                this.localOffset = localOffset;
            }
        }

        private readonly struct AutoAddResult
        {
            public readonly int originsAdded;
            public readonly int prefabsChanged;
            public readonly List<string> errors;

            public AutoAddResult(int originsAdded, int prefabsChanged, List<string> errors)
            {
                this.originsAdded = originsAdded;
                this.prefabsChanged = prefabsChanged;
                this.errors = errors;
            }
        }

        [Serializable]
        private sealed class FolderSettings
        {
            public List<string> folders = new List<string>();
        }

        [MenuItem("Tools/Cast Anchors/Auto Add Missing", priority = 110)]
        public static void AutoAddMissing()
        {
            CastAnchorFolderWindow.ShowWindow(CastAnchorFolderWindow.Action.AutoAddMissing);
        }

        [MenuItem("Tools/Cast Anchors/Validate and Export", priority = 120)]
        public static void ValidateAndExport()
        {
            CastAnchorFolderWindow.ShowWindow(CastAnchorFolderWindow.Action.ValidateAndExport);
        }

        internal static List<string> LoadConfiguredFolders()
        {
            string json = EditorPrefs.GetString(FolderPrefsKey, string.Empty);
            var settings = string.IsNullOrEmpty(json) ? null : JsonUtility.FromJson<FolderSettings>(json);
            return settings?.folders != null && settings.folders.Count > 0
                ? settings.folders.Distinct(StringComparer.Ordinal).ToList()
                : GetDefaultFolders();
        }

        internal static List<string> GetDefaultFolders() => DefaultPrefabFolders.ToList();

        internal static void SaveConfiguredFolders(List<string> folders)
        {
            EditorPrefs.SetString(FolderPrefsKey, JsonUtility.ToJson(new FolderSettings { folders = folders }));
        }

        internal static void RunAutoAddMissing(IReadOnlyList<string> prefabPaths, Vector3 localOffset, bool reapplyAll)
        {
            if (prefabPaths.Count == 0)
            {
                const string message = "대상 프리팹이 없습니다.";
                Debug.LogError("[CastAnchors] " + message);
                EditorUtility.DisplayDialog("Cast Anchors", message, "OK");
                return;
            }

            AutoAddResult result = AddMissingAnchors(prefabPaths, localOffset, reapplyAll);
            string mode = reapplyAll ? "전체 재적용" : "누락 프리팹만";
            string done = $"{mode} 완료\n변경 프리팹={result.prefabsChanged}\n"
                        + $"SkillCastOrigin 추가={result.originsAdded}";
            if (result.errors.Count > 0)
                done += $"\n\n오류 {result.errors.Count}건\n" + string.Join("\n", result.errors.Take(8));

            Debug.Log("[CastAnchors] " + done);
            EditorUtility.DisplayDialog("Cast Anchors", done, "OK");
        }

        internal static void RunValidateAndExport(IReadOnlyList<string> folders)
        {
            var errors = new List<string>();
            var warnings = new List<string>();
            List<PrefabAnchorEntry> prefabs = CollectFolderAnchors(errors, warnings, folders);
            List<AnchorEntry> players = CollectPlayers(errors, warnings);

            if (errors.Count > 0)
            {
                string message = $"CastAnchor export 실패 ({errors.Count}건)\n\n" + string.Join("\n", errors);
                Debug.LogError("[CastAnchors] " + message);
                EditorUtility.DisplayDialog("Cast Anchors", message, "OK");
                return;
            }

            string json = BuildJson(prefabs, players);
            string outputDir = Path.GetFullPath(Path.Combine(Application.dataPath, ServerOutputDir));
            Directory.CreateDirectory(outputDir);
            string path = Path.Combine(outputDir, OutputFileName);
            string tempPath = path + ".tmp";

            try
            {
                File.WriteAllText(tempPath, json, new UTF8Encoding(false));
                File.Copy(tempPath, path, true);
                File.Delete(tempPath);
            }
            catch (Exception ex)
            {
                Debug.LogError($"[CastAnchors] export 실패: {ex}");
                EditorUtility.DisplayDialog("Cast Anchors", $"파일 쓰기 실패\n{ex.Message}", "OK");
                return;
            }

            foreach (string warning in warnings)
                Debug.LogWarning("[CastAnchors] " + warning);

            string done = $"export 완료\nprefabs={prefabs.Count}, players={players.Count}\n{path}";
            if (warnings.Count > 0)
            {
                const int previewCount = 8;
                string preview = string.Join("\n", warnings.Take(previewCount));
                if (warnings.Count > previewCount)
                    preview += $"\n... 외 {warnings.Count - previewCount}건";
                done += $"\n\n경고 {warnings.Count}건: SkillCastOrigin이 없거나 유효하지 않은 프리팹은 기본값 (0, 1, 0)으로 export했습니다.\n{preview}";
            }
            Debug.Log("[CastAnchors] " + done);
            EditorUtility.DisplayDialog("Cast Anchors", done, "OK");
        }

        private static List<AnchorEntry> CollectPlayers(List<string> errors, List<string> warnings)
        {
            var result = new List<AnchorEntry>();
            foreach (CharacterFactory.PrefabProfile profile in CharacterFactory.GetSupportedPrefabProfiles())
            {
                AnchorReadResult readResult = TryReadAnchor(profile.PrefabPath, out Vector3 localOffset, out string message);
                if (readResult == AnchorReadResult.Error)
                {
                    errors.Add($"Player job={profile.JobId}, preset={profile.PresetId}, prefab={profile.PrefabPath}: {message}");
                    continue;
                }
                if (readResult == AnchorReadResult.UseDefault)
                {
                    localOffset = CastOriginUtility.DefaultLocalOffset;
                    warnings.Add($"Player job={profile.JobId}, preset={profile.PresetId}, prefab={profile.PrefabPath}: {message}");
                }
                result.Add(new AnchorEntry(profile.JobId, profile.PresetId, localOffset));
            }
            return result;
        }

        private static List<string> CollectTargetPrefabAssetPaths(List<string> errors, IEnumerable<string> folders)
        {
            var result = new HashSet<string>(StringComparer.Ordinal);
            foreach (string folder in folders)
            {
                if (!AssetDatabase.IsValidFolder(folder))
                {
                    errors.Add($"프리팹 폴더를 찾을 수 없습니다: {folder}");
                    continue;
                }

                foreach (string guid in AssetDatabase.FindAssets("t:Prefab", new[] { folder }))
                    result.Add(AssetDatabase.GUIDToAssetPath(guid));
            }

            return result.OrderBy(path => path, StringComparer.Ordinal).ToList();
        }

        private static List<PrefabAnchorEntry> CollectFolderAnchors(List<string> errors, List<string> warnings, IReadOnlyList<string> folders)
        {
            var result = new List<PrefabAnchorEntry>();
            foreach (string assetPath in CollectTargetPrefabAssetPaths(errors, folders))
            {
                string prefabPath = AssetPathToResourcesPath(assetPath);
                AnchorReadResult readResult = TryReadAnchor(prefabPath, out Vector3 localOffset, out string message);
                if (readResult == AnchorReadResult.Error)
                {
                    errors.Add($"Prefab={prefabPath}: {message}");
                    continue;
                }
                if (readResult == AnchorReadResult.UseDefault)
                {
                    localOffset = CastOriginUtility.DefaultLocalOffset;
                    warnings.Add($"Prefab={prefabPath}: {message}");
                }
                result.Add(new PrefabAnchorEntry(prefabPath, localOffset));
            }
            return result;
        }

        private static AutoAddResult AddMissingAnchors(IEnumerable<string> prefabPaths, Vector3 localOffset, bool reapplyAll)
        {
            int originsAdded = 0;
            int prefabsChanged = 0;
            var errors = new List<string>();

            foreach (string prefabPath in prefabPaths)
            {
                if (AssetDatabase.LoadAssetAtPath<GameObject>(prefabPath) == null)
                {
                    errors.Add($"프리팹을 찾을 수 없습니다: {prefabPath}");
                    continue;
                }

                GameObject root = null;
                try
                {
                    root = PrefabUtility.LoadPrefabContents(prefabPath);
                    bool changed = false;

                    Transform origin = FindExistingOrigin(root.transform);
                    bool originWasMissing = origin == null;
                    if (originWasMissing)
                    {
                        var originObject = new GameObject(CastOriginUtility.TransformName);
                        origin = originObject.transform;
                        origin.SetParent(root.transform, false);
                        ++originsAdded;
                        changed = true;
                    }

                    if (reapplyAll || originWasMissing)
                    {
                        origin.localPosition = localOffset;
                        changed = true;
                    }

                    if (!changed)
                        continue;

                    PrefabUtility.SaveAsPrefabAsset(root, prefabPath);
                    ++prefabsChanged;
                }
                catch (Exception ex)
                {
                    errors.Add($"{prefabPath}: {ex.Message}");
                }
                finally
                {
                    if (root != null)
                        PrefabUtility.UnloadPrefabContents(root);
                }
            }

            AssetDatabase.SaveAssets();
            return new AutoAddResult(originsAdded, prefabsChanged, errors);
        }

        private static Transform FindExistingOrigin(Transform root)
        {
            Transform origin = root.Find(CastOriginUtility.TransformName);
            return origin;
        }

        private static string AssetPathToResourcesPath(string assetPath)
        {
            const string resourcesPrefix = "Assets/Resources/";
            return assetPath.Substring(resourcesPrefix.Length, assetPath.Length - resourcesPrefix.Length - ".prefab".Length);
        }

        private static AnchorReadResult TryReadAnchor(string resourcesPath, out Vector3 localOffset, out string message)
        {
            localOffset = CastOriginUtility.DefaultLocalOffset;
            message = null;
            GameObject prefab = Resources.Load<GameObject>(resourcesPath);
            if (prefab == null)
            {
                message = "Resources prefab을 찾을 수 없습니다.";
                return AnchorReadResult.Error;
            }

            Transform origin = FindExistingOrigin(prefab.transform);
            if (origin == null)
            {
                message = "Root 직속 SkillCastOrigin이 없습니다.";
                return AnchorReadResult.UseDefault;
            }

            if (!CastOriginUtility.TryGetLocalOffset(origin, out localOffset))
            {
                message = "SkillCastOrigin의 로컬 좌표에 유효하지 않은 값이 있습니다.";
                return AnchorReadResult.UseDefault;
            }

            return AnchorReadResult.Valid;
        }

        private static string BuildJson(List<PrefabAnchorEntry> prefabs, List<AnchorEntry> players)
        {
            var sb = new StringBuilder();
            sb.AppendLine("{");
            sb.AppendLine("  \"schemaVersion\": 3,");
            sb.AppendLine("  \"prefabs\": [");
            for (int i = 0; i < prefabs.Count; ++i)
            {
                PrefabAnchorEntry entry = prefabs[i];
                sb.Append($"    {{ \"prefabPath\": \"{entry.prefabPath}\", \"localOffset\": {{ \"x\": {F(entry.localOffset.x)}, \"y\": {F(entry.localOffset.y)}, \"z\": {F(entry.localOffset.z)} }} }}");
                sb.AppendLine(i + 1 == prefabs.Count ? string.Empty : ",");
            }
            sb.AppendLine("  ],");
            sb.AppendLine("  \"players\": [");
            for (int i = 0; i < players.Count; ++i)
            {
                AnchorEntry entry = players[i];
                sb.Append($"    {{ \"job\": {entry.keyA}, \"preset\": {entry.keyB}, \"localOffset\": {{ \"x\": {F(entry.localOffset.x)}, \"y\": {F(entry.localOffset.y)}, \"z\": {F(entry.localOffset.z)} }} }}");
                sb.AppendLine(i + 1 == players.Count ? string.Empty : ",");
            }
            sb.AppendLine("  ]");
            sb.AppendLine("}");
            return sb.ToString();
        }

        private static string F(float value) => value.ToString("0.###", CultureInfo.InvariantCulture);
    }

    internal sealed class CastAnchorFolderWindow : EditorWindow
    {
        internal enum Action
        {
            AutoAddMissing,
            ValidateAndExport,
        }

        private Action m_action;
        private List<string> m_folders;
        private readonly List<GameObject> m_prefabs = new List<GameObject>();
        private int m_targetMode;
        private Vector3 m_localOffset = CastOriginUtility.DefaultLocalOffset;
        private Vector2 m_scroll;

        internal static void ShowWindow(Action action)
        {
            var window = GetWindow<CastAnchorFolderWindow>(true, "Cast Anchors", true);
            window.m_action = action;
            window.m_folders = CastAnchorExportMenu.LoadConfiguredFolders();
            window.m_prefabs.Clear();
            window.m_targetMode = 0;
            window.m_localOffset = CastOriginUtility.DefaultLocalOffset;
            window.minSize = new Vector2(500.0f, 330.0f);
            window.ShowUtility();
        }

        private void OnGUI()
        {
            string actionLabel = m_action == Action.AutoAddMissing ? "Auto Add Missing" : "Validate and Export";
            EditorGUILayout.LabelField(actionLabel, EditorStyles.boldLabel);
            EditorGUILayout.HelpBox("대상 폴더는 재귀적으로 탐색됩니다. 폴더 설정은 다음 실행에도 유지됩니다.", MessageType.Info);

            if (m_folders == null)
                m_folders = CastAnchorExportMenu.LoadConfiguredFolders();

            if (m_action == Action.AutoAddMissing)
            {
                m_localOffset = EditorGUILayout.Vector3Field("SkillCastOrigin 로컬 X/Y/Z", m_localOffset);
                EditorGUILayout.Space(4.0f);
                m_targetMode = GUILayout.Toolbar(m_targetMode, new[] { "폴더 대상", "프리팹 대상" });
            }

            bool useFolders = m_action == Action.ValidateAndExport || m_targetMode == 0;
            if (useFolders)
                DrawFolderTargets();
            else
                DrawPrefabTargets();

            EditorGUILayout.Space(8.0f);
            if (m_action == Action.AutoAddMissing)
            {
                using (new EditorGUI.DisabledScope(TargetCount(useFolders) == 0))
                {
                    if (GUILayout.Button("Missing 인 프리팹에만 작업 수행", GUILayout.Height(30.0f)))
                        RunAutoAdd(useFolders, reapplyAll: false);
                    if (GUILayout.Button("대상 프리팹에 모두 작업 다시 수행", GUILayout.Height(30.0f)))
                        RunAutoAdd(useFolders, reapplyAll: true);
                }
            }
            else
            {
                using (new EditorGUI.DisabledScope(m_folders.Count == 0))
                {
                    if (GUILayout.Button(actionLabel + " 실행", GUILayout.Height(30.0f)))
                    {
                        CastAnchorExportMenu.SaveConfiguredFolders(m_folders);
                        Close();
                        CastAnchorExportMenu.RunValidateAndExport(m_folders);
                    }
                }
            }
        }

        private void DrawFolderTargets()
        {
            EditorGUILayout.LabelField("대상 폴더", EditorStyles.boldLabel);
            m_scroll = EditorGUILayout.BeginScrollView(m_scroll, GUILayout.ExpandHeight(true));
            for (int i = 0; i < m_folders.Count; ++i)
            {
                EditorGUILayout.BeginHorizontal();
                EditorGUILayout.SelectableLabel(m_folders[i], EditorStyles.textField, GUILayout.Height(EditorGUIUtility.singleLineHeight));
                if (GUILayout.Button("삭제", GUILayout.Width(52.0f)))
                {
                    m_folders.RemoveAt(i);
                    --i;
                }
                EditorGUILayout.EndHorizontal();
            }
            EditorGUILayout.EndScrollView();

            EditorGUILayout.BeginHorizontal();
            if (GUILayout.Button("폴더 추가"))
                AddFolder();
            if (GUILayout.Button("기본값 복원"))
                m_folders = CastAnchorExportMenu.GetDefaultFolders();
            EditorGUILayout.EndHorizontal();
        }

        private void DrawPrefabTargets()
        {
            EditorGUILayout.LabelField("대상 프리팹", EditorStyles.boldLabel);
            Rect dropArea = GUILayoutUtility.GetRect(0.0f, 48.0f, GUILayout.ExpandWidth(true));
            GUI.Box(dropArea, "Prefab을 이곳에 드래그 & 드롭하세요", EditorStyles.helpBox);
            HandlePrefabDragAndDrop(dropArea);

            m_scroll = EditorGUILayout.BeginScrollView(m_scroll, GUILayout.ExpandHeight(true));
            for (int i = 0; i < m_prefabs.Count; ++i)
            {
                EditorGUILayout.BeginHorizontal();
                m_prefabs[i] = (GameObject)EditorGUILayout.ObjectField(m_prefabs[i], typeof(GameObject), false);
                if (GUILayout.Button("삭제", GUILayout.Width(52.0f)))
                {
                    m_prefabs.RemoveAt(i);
                    --i;
                }
                EditorGUILayout.EndHorizontal();
            }
            EditorGUILayout.EndScrollView();
        }

        private void RunAutoAdd(bool useFolders, bool reapplyAll)
        {
            List<string> prefabPaths;
            if (useFolders)
            {
                var errors = new List<string>();
                prefabPaths = CollectPrefabPathsFromFolders(m_folders, errors);
                if (errors.Count > 0)
                {
                    EditorUtility.DisplayDialog("Cast Anchors", string.Join("\n", errors), "OK");
                    return;
                }
                CastAnchorExportMenu.SaveConfiguredFolders(m_folders);
            }
            else
            {
                prefabPaths = m_prefabs.Where(prefab => prefab != null)
                    .Select(AssetDatabase.GetAssetPath)
                    .Where(path => !string.IsNullOrEmpty(path))
                    .Distinct(StringComparer.Ordinal)
                    .ToList();
            }

            Close();
            CastAnchorExportMenu.RunAutoAddMissing(prefabPaths, m_localOffset, reapplyAll);
        }

        private int TargetCount(bool useFolders)
        {
            return useFolders ? m_folders.Count : m_prefabs.Count(prefab => prefab != null);
        }

        private static List<string> CollectPrefabPathsFromFolders(IEnumerable<string> folders, List<string> errors)
        {
            var paths = new HashSet<string>(StringComparer.Ordinal);
            foreach (string folder in folders)
            {
                if (!AssetDatabase.IsValidFolder(folder))
                {
                    errors.Add($"프리팹 폴더를 찾을 수 없습니다: {folder}");
                    continue;
                }
                foreach (string guid in AssetDatabase.FindAssets("t:Prefab", new[] { folder }))
                    paths.Add(AssetDatabase.GUIDToAssetPath(guid));
            }
            return paths.OrderBy(path => path, StringComparer.Ordinal).ToList();
        }

        private void HandlePrefabDragAndDrop(Rect dropArea)
        {
            Event evt = Event.current;
            if (!dropArea.Contains(evt.mousePosition))
                return;

            if (evt.type == EventType.DragUpdated || evt.type == EventType.DragPerform)
            {
                DragAndDrop.visualMode = DragAndDropVisualMode.Copy;
                if (evt.type == EventType.DragPerform)
                {
                    DragAndDrop.AcceptDrag();
                    foreach (UnityEngine.Object item in DragAndDrop.objectReferences)
                    {
                        if (item is GameObject prefab && PrefabUtility.GetPrefabAssetType(prefab) != PrefabAssetType.NotAPrefab && !m_prefabs.Contains(prefab))
                            m_prefabs.Add(prefab);
                    }
                }
                evt.Use();
            }
        }

        private void AddFolder()
        {
            string absolutePath = EditorUtility.OpenFolderPanel("Cast Anchor 대상 폴더", Application.dataPath, string.Empty);
            if (string.IsNullOrEmpty(absolutePath))
                return;

            string assetPath = FileUtil.GetProjectRelativePath(absolutePath);
            if (!assetPath.StartsWith("Assets/", StringComparison.Ordinal) || !AssetDatabase.IsValidFolder(assetPath))
            {
                EditorUtility.DisplayDialog("Cast Anchors", "프로젝트 Assets 폴더 안의 유효한 폴더만 추가할 수 있습니다.", "OK");
                return;
            }

            if (!m_folders.Contains(assetPath, StringComparer.Ordinal))
                m_folders.Add(assetPath);
        }
    }
}
