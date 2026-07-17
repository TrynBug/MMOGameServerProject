using System.Collections.Generic;
using System.IO;
using System.Text;
using DotRecast.Core.Numerics;
using DotRecast.Detour;
using DotRecast.Detour.Io;
using MMO.Client.Navigation;
using UnityEditor;
using UnityEngine;

namespace MMO.Client.MapVerify.Editor
{
    /// <summary>
    /// 맵 스타일 무관 NavMesh/수면 자동 검증기 (MapGen 툴에서 분리, 2026-07-05).
    /// 스펙 에셋 없이 동작: 씬의 NavProbeMarker + StageStartPosition.csv 스폰 + River_ 수면 평면만 사용.
    /// 씬 이름 = 스테이지명 = NavMesh .bin 이름 규약을 따른다.
    /// </summary>
    public static class MapVerifier
    {
        [MenuItem("Tools/MapVerify/현재 씬 검증")]
        public static void VerifyActiveScene()
        {
            var sb = new StringBuilder();
            string stage = UnityEngine.SceneManagement.SceneManager.GetActiveScene().name;
            sb.AppendLine($"=== MapVerify: {stage} ===");
            int bad = 0;

            string binPath = Path.Combine(Application.streamingAssetsPath, $"NavMesh/{stage}.bin");
            if (!File.Exists(binPath))
            {
                Debug.LogWarning($"[MapVerify] bin 없음: {binPath} — NavMesh 빌드 먼저 실행하세요.");
                return;
            }
            using var fs = File.OpenRead(binPath);
            using var br = new BinaryReader(fs);
            var navMesh = new DtMeshSetReader().Read(br, 6);
            var query = new DtNavMeshQuery(navMesh);
            var filter = new DtQueryDefaultFilter();
            var ext = new RcVec3f(0.8f, 3f, 0.8f);

            (long refId, RcVec3f pt, float dxz) Probe(Vector3 wp)
            {
                var pos = new RcVec3f(wp.x, wp.y, wp.z);
                query.FindNearestPoly(pos, ext, filter, out long r, out var np, out _);
                if (r == 0) return (0, np, 999f);
                float d = Mathf.Sqrt((np.X - wp.x) * (np.X - wp.x) + (np.Z - wp.z) * (np.Z - wp.z));
                return (r, np, d);
            }

            // -- 1) 스폰 지점 (StageStartPosition.csv, NavMeshFileName == 씬 이름인 스테이지) --
            var spawns = LoadSpawns(stage);
            long spawnRef = 0; RcVec3f spawnPt = default;
            foreach (var sp in spawns)
            {
                var (r, np, d) = Probe(sp);
                if (r == 0 || d > 0.5f) { sb.AppendLine($"  [스폰] ({sp.x:F1},{sp.z:F1}) 통행불가!"); bad++; }
                else if (spawnRef == 0) { spawnRef = r; spawnPt = np; }
            }
            sb.AppendLine($"[스폰] {spawns.Count}개 검사");

            // -- 2) 씬 마커 프로브 --
            var markers = Object.FindObjectsByType<NavProbeMarker>(FindObjectsSortMode.None);
            int nW = 0, nB = 0, nP = 0;
            long[] buf = new long[2048];
            foreach (var m in markers)
            {
                var (r, np, d) = Probe(m.transform.position);
                switch (m.type)
                {
                    case NavProbeMarker.ProbeType.Walkable:
                        nW++;
                        if (r == 0 || d > 0.5f) { sb.AppendLine($"  [Walkable] {m.name} 실패!"); bad++; }
                        break;
                    case NavProbeMarker.ProbeType.Blocked:
                        nB++;
                        if (r != 0 && d < 1.0f) { sb.AppendLine($"  [Blocked] {m.name} 이 walkable!"); bad++; }
                        break;
                    case NavProbeMarker.ProbeType.PathFromSpawn:
                        nP++;
                        if (spawnRef == 0) { sb.AppendLine($"  [Path] 스폰 폴리 없음 — {m.name} 스킵"); bad++; break; }
                        if (r == 0) { sb.AppendLine($"  [Path] {m.name} 끝점 폴리 없음!"); bad++; break; }
                        var st = query.FindPath(spawnRef, r, spawnPt, np, filter, buf, out int cnt, 2048);
                        if (!st.Succeeded()) { sb.AppendLine($"  [Path] 스폰→{m.name} 실패!"); bad++; }
                        break;
                }
            }
            sb.AppendLine($"[마커] Walkable {nW} / Blocked {nB} / Path {nP}");

            // -- 3) 수면 평면 가장자리 누수 (River_ 규약 평면의 로컬 바운즈 가장자리 밖 0.5m 지형이 수면 아래면 누수) --
            var terrains = Object.FindObjectsByType<Terrain>(FindObjectsSortMode.None);
            if (terrains.Length > 0)
            {
                var terr = terrains[0];
                var planeList = new List<MeshFilter>();
                foreach (var mf in Object.FindObjectsByType<MeshFilter>(FindObjectsSortMode.None))
                    if (mf.sharedMesh != null && (mf.name.StartsWith("River_") || mf.sharedMesh.name.Contains("River_Plane"))) planeList.Add(mf);
                // 다른 평면이 덮고 있으면 누수 아님 (겹침 타일링 대응)
                bool Covered(Vector3 wp)
                {
                    foreach (var p in planeList)
                    {
                        Vector3 lp = p.transform.InverseTransformPoint(wp);
                        var pb = p.sharedMesh.bounds;
                        if (lp.x >= pb.min.x - 0.05f && lp.x <= pb.max.x + 0.05f &&
                            lp.z >= pb.min.z - 0.05f && lp.z <= pb.max.z + 0.05f) return true;
                    }
                    return false;
                }
                int leaks = 0;
                foreach (var mf in planeList)
                {
                    var b = mf.sharedMesh.bounds;
                    float waterY = mf.transform.position.y;
                    for (float t = 0.05f; t < 1f; t += 0.12f)
                    {
                        foreach (var edge in EdgePoints(b, t))
                        {
                            Vector3 e = mf.transform.TransformPoint(edge);
                            Vector3 c = mf.transform.TransformPoint(b.center);
                            Vector3 outward = (e - c); outward.y = 0;
                            Vector3 q = e + outward.normalized * 0.5f;
                            if (terr.SampleHeight(q) < waterY - 0.12f && !Covered(q)) { leaks++; if (leaks <= 3) sb.AppendLine($"  [수면] 누수 ({q.x:F0},{q.z:F0})"); }
                        }
                    }
                }
                if (leaks > 0) bad += leaks;
                sb.AppendLine($"[수면] 평면 {planeList.Count}장, 누수 {leaks}점");
            }

            sb.AppendLine(bad == 0 ? ">>> 전체 통과" : $">>> 문제 {bad}건");
            if (bad == 0) Debug.Log(sb.ToString());
            else Debug.LogWarning(sb.ToString());
        }

        static IEnumerable<Vector3> EdgePoints(Bounds b, float t)
        {
            yield return new Vector3(Mathf.Lerp(b.min.x, b.max.x, t), 0, b.min.z);
            yield return new Vector3(Mathf.Lerp(b.min.x, b.max.x, t), 0, b.max.z);
            yield return new Vector3(b.min.x, 0, Mathf.Lerp(b.min.z, b.max.z, t));
            yield return new Vector3(b.max.x, 0, Mathf.Lerp(b.min.z, b.max.z, t));
        }

        /// <summary>Stage.csv 에서 NavMeshFileName == 씬 이름인 스테이지 키를 찾아 그 스폰 좌표들을 반환.</summary>
        static List<Vector3> LoadSpawns(string stageName)
        {
            var result = new List<Vector3>();
            try
            {
                string gd = Path.Combine(Application.streamingAssetsPath, "GameData");
                var keys = new HashSet<string>();
                var stageLines = File.ReadAllLines(Path.Combine(gd, "Stage.csv"));
                for (int i = 1; i < stageLines.Length; i++)
                {
                    var c = stageLines[i].Split(',');
                    if (c.Length >= 5 && c[4].Trim() == stageName) keys.Add(c[0].Trim());
                }
                var posLines = File.ReadAllLines(Path.Combine(gd, "StageStartPosition.csv"));
                for (int i = 1; i < posLines.Length; i++)
                {
                    var c = posLines[i].Split(',');
                    if (c.Length >= 7 && keys.Contains(c[1].Trim()))
                        result.Add(new Vector3(float.Parse(c[3]), float.Parse(c[4]), float.Parse(c[5])));
                }
            }
            catch (System.Exception e) { Debug.LogWarning($"[MapVerify] 스폰 로드 실패: {e.Message}"); }
            return result;
        }
    }
}
