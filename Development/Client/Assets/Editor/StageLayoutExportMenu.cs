using System.Globalization;
using System.IO;
using System.Text;
using Client.Game;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

namespace Client.EditorTools
{
    // 활성 씬에 배치된 마커(Spawner/SpawnPoint/EventArea/Waypoint)를 스캔해
    // 서버 레이아웃 json(Map/StageLayout/<sceneName>.json)으로 export 한다.
    //
    // NavMesh 빌드와 동일 컨벤션: 파일명 = 활성 씬 이름(예: Town.unity -> Town.json).
    // 게임데이터 GameData_Stage.StageLayoutFileName 을 씬 이름과 동일하게 세팅해두면 서버가 이 파일을 로드한다.
    //
    // 출력은 서버 OUTPUT 만 — 클라는 런타임 스테이지 프리팹의 EventAreaMarker 를 직접 쓰므로 json 불필요.
    //
    // 진입점:
    //  1) 메뉴 Tools/StageLayout/Export Active Scene
    //  2) AIEditorBridge 의 EXPORT_STAGELAYOUT 명령(리플렉션으로 ExportScene 호출)
    public static class StageLayoutExportMenu
    {
        // Application.dataPath(=Assets) 기준 서버 출력 경로. NavMesh(../../Server/OUTPUT/Map/NavMesh)와 동형.
        private const string ServerOutputDir = "../../Server/OUTPUT/Map/StageLayout";

        public struct ExportResult
        {
            public bool   success;
            public string message;
            public string path;
        }

        [MenuItem("Tools/StageLayout/Export Active Scene", priority = 100)]
        public static void ExportActiveScene()
        {
            string sceneName = EditorSceneManager.GetActiveScene().name;
            if (string.IsNullOrEmpty(sceneName))
            {
                EditorUtility.DisplayDialog("StageLayout Export", "활성 씬이 저장되지 않았습니다. 먼저 씬을 저장해주세요.", "OK");
                return;
            }

            ExportResult r = ExportScene(sceneName);
            EditorUtility.DisplayDialog("StageLayout Export", r.message, "OK");
        }

        // AIEditorBridge 가 리플렉션으로 호출(시그니처 고정 — 이름/파라미터/반환 타입 변경 주의).
        public static ExportResult ExportScene(string sceneName)
        {
            if (string.IsNullOrEmpty(sceneName))
                sceneName = EditorSceneManager.GetActiveScene().name;
            if (string.IsNullOrEmpty(sceneName))
                return new ExportResult { success = false, message = "활성 씬이 저장되지 않았습니다." };

            SpawnerMarker[]    spawners    = Object.FindObjectsByType<SpawnerMarker>(FindObjectsInactive.Exclude);
            SpawnPointMarker[] spawnPoints = Object.FindObjectsByType<SpawnPointMarker>(FindObjectsInactive.Exclude);
            EventAreaMarker[]  eventAreas  = Object.FindObjectsByType<EventAreaMarker>(FindObjectsInactive.Exclude);
            WaypointMarker[]   waypoints   = Object.FindObjectsByType<WaypointMarker>(FindObjectsInactive.Exclude);

            string json = BuildJson(spawners, spawnPoints, eventAreas, waypoints);

            string serverDir = Path.GetFullPath(Path.Combine(Application.dataPath, ServerOutputDir));
            Directory.CreateDirectory(serverDir);
            string path = Path.Combine(serverDir, sceneName + ".json");

            try
            {
                File.WriteAllText(path, json, new UTF8Encoding(false));
            }
            catch (System.Exception ex)
            {
                return new ExportResult { success = false, message = $"파일 쓰기 실패: {ex.Message}" };
            }

            AssetDatabase.Refresh();

            string msg = $"export 완료: {sceneName}.json\n" +
                         $"spawners={spawners.Length}, spawnPoints={spawnPoints.Length}, " +
                         $"eventAreas={eventAreas.Length}, waypoints={waypoints.Length}\n{path}";
            Debug.Log($"[StageLayoutExport] {msg}");
            return new ExportResult { success = true, message = msg, path = path };
        }

        // ── JSON 빌드 (서버 StageLayout 파서 포맷에 맞춰 핸드빌드) ─────────────────

        private static string BuildJson(SpawnerMarker[] spawners, SpawnPointMarker[] spawnPoints,
                                        EventAreaMarker[] eventAreas, WaypointMarker[] waypoints)
        {
            var sb = new StringBuilder();
            sb.Append("{\n");

            // spawners
            sb.Append("  \"spawners\": [");
            for (int i = 0; i < spawners.Length; ++i)
            {
                SpawnerMarker s = spawners[i];
                sb.Append(i == 0 ? "\n" : ",\n");
                sb.Append($"    {{ \"key\": {s.Key}, \"pos\": {Vec3(s.transform.position)}, \"radius\": {F(s.Radius)} }}");
            }
            sb.Append(spawners.Length > 0 ? "\n  ],\n" : "],\n");

            // spawnPoints
            sb.Append("  \"spawnPoints\": [");
            for (int i = 0; i < spawnPoints.Length; ++i)
            {
                SpawnPointMarker sp = spawnPoints[i];
                sb.Append(i == 0 ? "\n" : ",\n");
                sb.Append($"    {{ \"key\": {sp.Key}, \"pos\": {Vec3(sp.transform.position)}, \"yaw\": {F(sp.transform.eulerAngles.y)} }}");
            }
            sb.Append(spawnPoints.Length > 0 ? "\n  ],\n" : "],\n");

            // eventAreas
            sb.Append("  \"eventAreas\": [");
            for (int i = 0; i < eventAreas.Length; ++i)
            {
                EventAreaMarker e = eventAreas[i];
                sb.Append(i == 0 ? "\n" : ",\n");
                sb.Append($"    {{ \"key\": {e.EventKey}, \"shape\": \"{(e.Shape == EAreaShape.Box ? "Box" : "Sphere")}\", ");
                sb.Append($"\"center\": {Vec3(e.transform.position)}, ");
                if (e.Shape == EAreaShape.Box)
                    sb.Append($"\"size\": [{F(e.SizeX)}, 0, {F(e.SizeZ)}], ");
                else
                    sb.Append($"\"radius\": {F(e.Radius)}, ");
                sb.Append($"\"secure\": {(e.Secure ? "true" : "false")} }}");
            }
            sb.Append(eventAreas.Length > 0 ? "\n  ],\n" : "],\n");

            // waypoints
            sb.Append("  \"waypoints\": [");
            for (int i = 0; i < waypoints.Length; ++i)
            {
                WaypointMarker w = waypoints[i];
                sb.Append(i == 0 ? "\n" : ",\n");
                sb.Append($"    {{ \"key\": {w.Key}, \"points\": [");
                int p = 0;
                foreach (Transform child in w.transform)
                {
                    sb.Append(p == 0 ? "" : ", ");
                    sb.Append(Vec3(child.position));
                    ++p;
                }
                sb.Append("] }");
            }
            sb.Append(waypoints.Length > 0 ? "\n  ]\n" : "]\n");

            sb.Append("}\n");
            return sb.ToString();
        }

        // 로케일 무관(invariant) float 포맷 — 일부 로케일의 콤마 소수점 방지.
        private static string F(float v) => v.ToString("0.###", CultureInfo.InvariantCulture);
        private static string Vec3(Vector3 v) => $"[{F(v.x)}, {F(v.y)}, {F(v.z)}]";
    }
}
