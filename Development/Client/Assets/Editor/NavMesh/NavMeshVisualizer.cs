using System.IO;
using DotRecast.Core;
using DotRecast.Detour;
using DotRecast.Detour.Io;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;

namespace MMO.Client.Navigation.Editor
{
    /// <summary>
    /// 빌드된 .bin NavMesh 를 Unity Scene View 에 오버레이로 그려주는 디버그 도구.
    ///
    /// 왜 필요한가:
    /// - NavMesh 가 정상적으로 빌드됐는지 시각적으로 확인하지 않으면 빌드 파라미터 튜닝이 어렵다.
    /// - 폴리곤이 끊겼는지, 좁은 통로가 막혔는지, 가파른 경사가 제외됐는지 등은 보지 않으면 알기 어렵다.
    /// - Recast 의 표준 데모도 동일한 방식의 디버그 드로잉을 제공한다.
    ///
    /// 동작 방식:
    /// 1. [InitializeOnLoad] 로 에디터 로드 시 자동 등록.
    /// 2. SceneView.duringSceneGui 이벤트에 OnSceneGUI 를 구독해서 매 프레임 그리기 콜백 받음.
    /// 3. 활성 씬 이름과 동일한 .bin 파일을 찾아 로드 후 폴리곤을 그림.
    /// 4. 메뉴로 ON/OFF 토글.
    ///
    /// 메뉴:
    /// - Tools/NavMesh/Show Visualizer  : 시각화 켜기 + 강제 재로드
    /// - Tools/NavMesh/Hide Visualizer  : 시각화 끄기
    /// </summary>
    [InitializeOnLoad]
    public static class NavMeshVisualizer
    {
        // -- 시각화 ON/OFF 상태 --
        // s_enabled 가 true 일 때만 그린다. 도구 켜기/끄기 메뉴로 토글.
        private static bool s_enabled;

        // -- 로드된 NavMesh 캐시 --
        // 매 프레임 다시 로드하면 디스크 IO 가 낭비되므로 한 번 로드해두고 캐싱.
        // 씬 이름이 바뀌면 ReloadIfNeeded 가 알아서 다시 로드.
        private static DtNavMesh s_navMesh;
        private static string s_loadedStage;

        // -- 시각화 색상 --
        // Color 의 4번째 값은 알파(투명도). 알파가 낮은 채움 + 알파가 높은 가장자리로
        // "투명하지만 윤곽은 또렷한" 표현을 만든다.
        private static readonly Color s_polyFill = new Color(0.0f, 0.7f, 1.0f, 0.25f);
        private static readonly Color s_polyEdge = new Color(0.0f, 0.5f, 0.8f, 0.9f);

        // ---------------------------------------------------------------------
        // 정적 생성자 ([InitializeOnLoad] 효과)
        // ---------------------------------------------------------------------
        // Unity 에디터 로드 시점, 그리고 스크립트 컴파일 직후 자동 호출된다.
        // SceneView.duringSceneGui 에 콜백을 등록해 매 프레임 OnSceneGUI 가 호출되도록 함.
        //
        // 주의: 컴파일이 반복되면 이 생성자가 여러 번 호출될 수 있는데,
        // 이벤트 핸들러 등록은 한 번만 되도록 Unity 가 도메인 리로드 시 자동 정리한다.
        // ---------------------------------------------------------------------
        static NavMeshVisualizer()
        {
            SceneView.duringSceneGui += OnSceneGUI;
        }

        // ---------------------------------------------------------------------
        // 메뉴: 시각화 켜기 (강제 재로드 포함)
        // ---------------------------------------------------------------------
        // 빌드 직후에 메뉴를 누르는 흐름이 가장 잦으므로, 매 호출 시 캐시를 비우고 다시 로드한다.
        // verbose=true 로 로드 결과를 콘솔에 명시적으로 출력 (성공 시에도 통계 표시).
        // ---------------------------------------------------------------------
        [MenuItem("Tools/NavMesh/Show Visualizer", priority = 200)]
        public static void Show()
        {
            s_enabled = true;
            // 캐시 무효화 -> 무조건 다시 로드
            s_loadedStage = null;
            s_navMesh = null;
            ReloadIfNeeded(verbose: true);
            // 모든 Scene View 를 갱신해서 시각화가 즉시 보이게
            SceneView.RepaintAll();
        }

        // ---------------------------------------------------------------------
        // 메뉴: 시각화 끄기
        // ---------------------------------------------------------------------
        [MenuItem("Tools/NavMesh/Hide Visualizer", priority = 201)]
        public static void Hide()
        {
            s_enabled = false;
            SceneView.RepaintAll();
        }

        // ---------------------------------------------------------------------
        // 필요 시 .bin 파일을 다시 로드한다.
        // ---------------------------------------------------------------------
        // 호출 시점:
        // 1. Show() 메뉴 직후 (verbose=true)
        // 2. OnSceneGUI 가 호출될 때마다 (verbose=false, 조용히 동작)
        //
        // 캐시 무효화 조건:
        // - 활성 씬 이름이 마지막 로드 시점과 다를 때
        // - 마지막 로드 결과가 null (이전 시도 실패)
        // ---------------------------------------------------------------------
        // Game 씬(스테이지 프리팹을 런타임에 동적 로드하는 실제 게임플레이 씬)의 이름.
        private const string GameSceneName = "Game";

        private static void ReloadIfNeeded(bool verbose = false)
        {
            string stage = EditorSceneManager.GetActiveScene().name;

            // Game 씬 특례:
            // Game 씬은 스테이지를 런타임에 프리팹으로 동적 로드하므로, 씬 이름("Game")에
            // 해당하는 .bin 은 존재하지 않는다. 이 경우 NavMeshService 에 "현재 로드된 스테이지"
            // 의 NavMesh(= 동적 로드된 스테이지에 대응하는 NavMesh)를 시각화한다.
            // 플레이 중 스테이지에 진입해 NavMesh 가 로드된 뒤에만 표시된다.
            if (stage == GameSceneName)
            {
                if (NavMeshService.IsLoaded)
                {
                    stage = NavMeshService.CurrentStageName;
                }
                else
                {
                    if (verbose)
                        Debug.Log("[NavMeshVisualizer] Game 씬: 아직 로드된 스테이지 NavMesh 가 없습니다. 플레이 중 스테이지에 진입한 뒤 표시됩니다.");
                    s_navMesh = null;
                    s_loadedStage = null;
                    return;
                }
            }

            if (string.IsNullOrEmpty(stage))
            {
                if (verbose) Debug.LogWarning("[NavMeshVisualizer] 활성 씬 이름이 비어있습니다.");
                s_navMesh = null;
                s_loadedStage = null;
                return;
            }

            // 이미 같은 씬을 로드해뒀으면 그대로 사용
            if (stage == s_loadedStage && s_navMesh != null)
                return;

            // 설정 에셋이 있어야 출력 경로를 알 수 있음
            var settings = NavMeshBuildMenu.FindSettings();
            if (settings == null)
            {
                if (verbose) Debug.LogWarning("[NavMeshVisualizer] NavMeshBuildSettings 를 찾을 수 없습니다.");
                return;
            }

            // 빌드 시와 동일한 경로 규칙으로 .bin 파일 위치 계산
            string clientDir = Path.GetFullPath(Path.Combine(Application.dataPath, settings.clientOutputDir));
            string path = Path.Combine(clientDir, stage + ".bin");
            if (!File.Exists(path))
            {
                if (verbose) Debug.LogWarning($"[NavMeshVisualizer] .bin 파일을 찾을 수 없습니다: {path}");
                s_navMesh = null;
                s_loadedStage = null;
                return;
            }

            try
            {
                var reader = new DtMeshSetReader();
                using var fs = File.OpenRead(path);
                using var br = new BinaryReader(fs);

                // 로드:
                // DotRecast Writer 는 tileRef 를 64bit 로 쓰므로 일반 Read 를 사용한다.
                // (만약 32bit tileRef 로 작성된 .bin 을 읽어야 한다면 Read32Bit 사용)
                // 두 번째 인자 6 = maxVertsPerPoly. 빌더와 동일하게 6 유지.
                s_navMesh = reader.Read(br, 6);
                s_loadedStage = stage;

                if (verbose)
                {
                    // 로드 결과 통계 출력 (디버깅에 매우 유용)
                    int polyTotal = 0;
                    int tileCount = 0;
                    int maxTiles = s_navMesh.GetMaxTiles();
                    for (int i = 0; i < maxTiles; i++)
                    {
                        var t = s_navMesh.GetTile(i);
                        if (t == null || t.data == null || t.data.header == null) continue;
                        tileCount++;
                        polyTotal += t.data.header.polyCount;
                    }
                    Debug.Log($"[NavMeshVisualizer] 로드 성공: {path}\n  tiles={tileCount}, polys={polyTotal}");
                }
            }
            catch (System.Exception ex)
            {
                Debug.LogWarning($"[NavMeshVisualizer] 로드 실패 ({path}): {ex.Message}\n{ex.StackTrace}");
                s_navMesh = null;
                s_loadedStage = null;
            }
        }

        // ---------------------------------------------------------------------
        // Scene View 그리기 콜백
        // ---------------------------------------------------------------------
        // Unity 가 Scene View 를 그릴 때마다 호출된다. 1 프레임 = 1 호출.
        // 여기서 Handles API 로 입체 그래픽(원/선/폴리곤)을 그릴 수 있다.
        //
        // 주의: 매 프레임 호출이므로 무거운 작업은 금지.
        // 로드는 캐시되어 있고, 매번 그리는 것은 가벼운 폴리곤 그리기뿐이다.
        // ---------------------------------------------------------------------
        private static void OnSceneGUI(SceneView view)
        {
            if (!s_enabled)
                return;

            // 씬을 바꾸거나 빌드 직후엔 자동으로 다시 로드 시도 (verbose=false 라 조용함)
            ReloadIfNeeded();
            if (s_navMesh == null)
                return;

            // 모든 타일을 순회하며 그리기
            // GetMaxTiles 는 사전 할당된 타일 슬롯 수 (실제 사용은 더 적을 수 있음).
            int maxTiles = s_navMesh.GetMaxTiles();
            for (int i = 0; i < maxTiles; i++)
            {
                DtMeshTile tile = s_navMesh.GetTile(i);
                // 사용되지 않는 슬롯은 null 이거나 header 가 비어있다
                if (tile == null || tile.data == null || tile.data.header == null)
                    continue;

                DrawTile(tile);
            }
        }

        // ---------------------------------------------------------------------
        // 한 타일의 모든 폴리곤을 그린다.
        // ---------------------------------------------------------------------
        // 타일 데이터 구조:
        // - data.header: 타일 메타 (bmin/bmax, polyCount, vertCount 등)
        // - data.polys[]: 폴리곤 배열. 각 폴리곤은 verts[](정점 인덱스), vertCount, area, flags 보유.
        // - data.verts[]: 모든 정점의 평탄화된 좌표 (x, y, z, x, y, z, ...).
        //
        // 폴리곤은 볼록 다각형(3~maxVertsPerPoly 정점)이며, "정점 인덱스 배열"로 표현된다.
        // 실제 좌표는 verts[poly.verts[j] * 3 + (0|1|2)] 로 접근한다.
        // ---------------------------------------------------------------------
        private static void DrawTile(DtMeshTile tile)
        {
            var data = tile.data;
            var header = data.header;

            // -- 디버그용: 타일 중심에 노란 원 표시 --
            // 폴리곤이 안 그려져도 노란 원만 보이면 "데이터는 잘 들어왔는데 그리기에 문제" 라는 진단 가능.
            // header.bmin/bmax 는 타일의 월드 좌표 바운딩 박스.
            Vector3 tileCenter = new Vector3(
                (header.bmin.X + header.bmax.X) * 0.5f,
                header.bmax.Y + 0.5f,           // 타일 상단 위로 살짝 띄움
                (header.bmin.Z + header.bmax.Z) * 0.5f);
            Handles.color = Color.yellow;
            Handles.DrawWireDisc(tileCenter, Vector3.up, 0.3f);

            // -- 각 폴리곤 그리기 --
            for (int p = 0; p < header.polyCount; p++)
            {
                var poly = data.polys[p];

                // Off-mesh connection (점프/텔레포트 링크) 은 면이 아니라 점-점 연결이므로 스킵.
                if (poly.GetPolyType() == DtPolyTypes.DT_POLYTYPE_OFFMESH_CONNECTION)
                    continue;

                int vc = poly.vertCount;
                if (vc < 3)
                    continue;  // 면이 아닌 것은 그리지 않음

                // 폴리곤 정점들의 월드 좌표 수집.
                // y 에 0.05 살짝 더해서 지면과 z-fighting 방지 (시각화 전용 트릭).
                Vector3[] pts = new Vector3[vc];
                for (int j = 0; j < vc; j++)
                {
                    int vi = poly.verts[j] * 3;
                    pts[j] = new Vector3(data.verts[vi], data.verts[vi + 1] + 0.05f, data.verts[vi + 2]);
                }

                // -- 1. 폴리곤 내부 채우기 --
                // Handles.DrawAAConvexPolygon 은 알파블렌딩 + 안티앨리어싱 된 삼각형을 그린다.
                // 다각형은 삼각형 부채꼴(triangle fan) 로 분해해서 그린다: [0,1,2], [0,2,3], [0,3,4]...
                Handles.color = s_polyFill;
                for (int j = 2; j < vc; j++)
                {
                    Handles.DrawAAConvexPolygon(pts[0], pts[j - 1], pts[j]);
                }

                // -- 2. 폴리곤 가장자리 (윤곽선) --
                // 마지막 정점 -> 첫 정점으로 닫기 위해 (j+1) % vc 사용.
                // 마지막 인자 2.0f 는 선 두께 (픽셀 단위, Scene View 줌과 무관).
                Handles.color = s_polyEdge;
                for (int j = 0; j < vc; j++)
                {
                    Vector3 a = pts[j];
                    Vector3 b = pts[(j + 1) % vc];
                    Handles.DrawLine(a, b, 2.0f);
                }
            }
        }
    }
}
