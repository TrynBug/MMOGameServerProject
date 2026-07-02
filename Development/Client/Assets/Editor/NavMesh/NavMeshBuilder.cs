using System;
using System.Collections.Generic;
using System.IO;
using DotRecast.Core;
using DotRecast.Core.Numerics;
using DotRecast.Detour;
using DotRecast.Detour.Io;
using DotRecast.Recast;
using DotRecast.Recast.Geom;
using UnityEditor;
using UnityEngine;

namespace MMO.Client.Navigation.Editor
{
    /// <summary>
    /// 씬에서 NavMeshSource 가 붙은 게임오브젝트의 메시를 수집하여 DotRecast 로 NavMesh 를 빌드하고
    /// .bin 파일로 저장하는 정적 유틸리티.
    ///
    /// 전체 빌드 파이프라인 개요:
    ///   1. 씬에서 walkable 메시(MeshFilter)를 수집해 정점/인덱스 배열로 모은다.
    ///   2. RcSampleInputGeomProvider 로 DotRecast 입력 객체 생성.
    ///   3. RcConfig 로 빌드 파라미터 구성.
    ///   4. RcBuilder.BuildTiles() 로 타일 단위 빌드 -> List&lt;RcBuilderResult&gt;.
    ///   5. DtNavMesh 를 초기화하고 각 타일 결과를 DtNavMeshBuilder.CreateNavMeshData() 로
    ///      네비메시 타일 데이터(DtMeshData) 로 변환 후 navMesh.AddTile() 로 등록.
    ///   6. DtMeshSetWriter 로 .bin 파일에 저장.
    ///
    /// Recast/Detour 용어 미니 사전:
    ///   - Recast: 입력 메시 -> 네비메시 빌드 도구. 빌드 타임 라이브러리.
    ///   - Detour: 빌드된 네비메시를 사용한 길찾기 라이브러리. 런타임 라이브러리.
    ///   - RcXxx: Recast 쪽 (Rc 접두어). RcConfig, RcBuilder, RcPolyMesh 등.
    ///   - DtXxx: Detour 쪽 (Dt 접두어). DtNavMesh, DtMeshData 등.
    ///   - Tile: 네비메시를 격자 단위로 분할한 한 조각.
    ///   - Polygon: walkable 영역을 다각형으로 표현한 단위. 길찾기는 폴리곤 단위로 동작.
    ///   - Voxel: 빌드 중간 단계의 3D 격자 셀. cellSize/cellHeight 로 크기 결정.
    /// </summary>
    public static class NavMeshBuilder
    {
        /// <summary>
        /// 빌드 결과를 호출자에게 돌려주기 위한 단순 DTO.
        /// 다이얼로그 출력, AIEditorBridge 결과 보고 등에 공통 사용된다.
        /// </summary>
        public class BuildResult
        {
            public bool success;
            public string message;
            public string clientPath;
            public string serverPath;
            public int tileCount;
            public int totalPolyCount;
        }

        /// <summary>
        /// 활성 씬에서 NavMesh 를 빌드하고 .bin 파일로 저장한다.
        /// 진입점은 NavMeshBuildMenu (메뉴) 와 AIEditorBridge (BUILD_NAVMESH 명령) 두 곳.
        /// </summary>
        /// <param name="settings">빌드 파라미터와 출력 경로를 담은 ScriptableObject</param>
        /// <param name="stageName">출력 파일명에 사용할 스테이지 이름 (보통 씬 이름)</param>
        public static BuildResult Build(NavMeshBuildSettings settings, string stageName)
        {
            var result = new BuildResult();

            // -- 입력 검증 --
            // 핵심 인자가 비어있으면 즉시 실패. 호출자에서 다이얼로그 등으로 처리.
            if (settings == null)
            {
                result.success = false;
                result.message = "NavMeshBuildSettings 가 null 입니다.";
                return result;
            }

            if (string.IsNullOrEmpty(stageName))
            {
                result.success = false;
                result.message = "stageName 이 비어있습니다.";
                return result;
            }

            // =================================================================
            // 1. 씬에서 입력 지오메트리 수집
            // -----------------------------------------------------------------
            // CollectGeometry 가 NavMeshSource 컴포넌트가 붙은 오브젝트 트리를 순회하며
            // 모든 MeshFilter 의 메시를 월드 좌표로 변환해서 단일 정점/인덱스 배열로 모은다.
            // Recast 는 "하나의 큰 삼각형 수프" 형태의 입력을 받으므로 이렇게 평탄화한다.
            // =================================================================
            if (!CollectGeometry(out float[] vertices, out int[] indices, out string collectMsg))
            {
                result.success = false;
                result.message = collectMsg;
                return result;
            }

            // =================================================================
            // 2. DotRecast 의 입력 지오메트리 객체 생성
            // -----------------------------------------------------------------
            // RcSampleInputGeomProvider 는 DotRecast 가 제공하는 표준 입력 구현체.
            // 생성자에서 bounds(bmin/bmax) 와 법선 등을 자동 계산한다.
            // 우리는 "씬 메시를 평탄화한 정점/인덱스 배열" 만 넘기면 된다.
            // =================================================================
            var geom = new RcSampleInputGeomProvider(vertices, indices);

            // =================================================================
            // 3. RcConfig (Recast 빌드 설정) 구성
            // -----------------------------------------------------------------
            // RcConfig 는 빌드의 모든 파라미터를 담는 컨테이너.
            // 타일 빌드(useTiles=true) 용 생성자를 사용한다.
            // borderSize 는 타일 경계에서 정확한 빌드를 위해 추가로 래스터화할 영역 크기.
            // regionMinArea/MergeArea 는 "복셀 한 변 수" 가 아니라 "월드 단위 면적" 으로 넘겨야 함.
            // =================================================================
            int tileSizeVx = settings.tileSize;
            int borderSize = RcConfig.CalcBorder(settings.agentRadius, settings.cellSize);

            // "복셀 한 변" 단위를 "월드 면적(m^2)" 단위로 변환
            // (RcConfig 내부에서 다시 복셀 수로 변환하므로 둘 다 알아야 함)
            float regionMinAreaWu = settings.regionMinSize * settings.regionMinSize * settings.cellSize * settings.cellSize;
            float regionMergeAreaWu = settings.regionMergeSize * settings.regionMergeSize * settings.cellSize * settings.cellSize;

            var cfg = new RcConfig(
                useTiles: true,
                tileSizeX: tileSizeVx,
                tileSizeZ: tileSizeVx,
                borderSize: borderSize,
                partition: RcPartition.WATERSHED,    // Watershed 가 가장 안정적이고 권장되는 영역 분할 방식
                cellSize: settings.cellSize,
                cellHeight: settings.cellHeight,
                agentMaxSlope: settings.agentMaxSlope,
                agentHeight: settings.agentHeight,
                agentRadius: settings.agentRadius,
                agentMaxClimb: settings.agentMaxClimb,
                minRegionArea: regionMinAreaWu,
                mergeRegionArea: regionMergeAreaWu,
                edgeMaxLen: settings.edgeMaxLen,
                edgeMaxError: settings.edgeMaxError,
                vertsPerPoly: settings.vertsPerPoly,
                detailSampleDist: settings.detailSampleDist,
                detailSampleMaxError: settings.detailSampleMaxError,
                filterLowHangingObstacles: true,     // 낮은 장애물(작은 돌멩이 등) 위는 walkable 로 처리
                filterLedgeSpans: true,              // 절벽 가장자리 안전 처리 (떨어지는 곳)
                filterWalkableLowHeightSpans: true,  // 천장이 너무 낮은 곳 제외
                walkableAreaMod: new RcAreaModification(1), // 모든 walkable 영역에 area id = 1 부여
                buildMeshDetail: true);              // detail mesh 생성 여부

            // =================================================================
            // 4. 타일별 빌드 실행
            // -----------------------------------------------------------------
            // RcBuilder.BuildTiles() 는 입력 지오메트리의 bounds 를 자동으로 계산해
            // tileSize 격자로 나누고 각 타일에 대해 빌드를 수행한다.
            // 결과는 타일당 1개의 RcBuilderResult (PolyMesh + PolyMeshDetail) 리스트.
            // buildAll=true 면 비어있는 타일도 결과에 포함 (Mesh=null 일 수 있음).
            // =================================================================
            var rcBuilder = new RcBuilder();
            List<RcBuilderResult> tileResults = rcBuilder.BuildTiles(geom, cfg, keepInterResults: false, buildAll: true);

            // =================================================================
            // 5. DtNavMesh (런타임 네비메시 객체) 초기화
            // -----------------------------------------------------------------
            // DtNavMesh 는 길찾기에 사용되는 런타임 네비메시.
            // Init() 시점에 "최대 몇 개의 타일을 담을 수 있는지", "타일당 폴리곤 최대 몇 개인지" 등
            // 메모리 풀 사이즈를 미리 정한다 (런타임 동적 확장 안 됨).
            //
            // tileWidth/tileHeight 는 타일 한 변의 월드 단위 크기.
            // maxTiles 는 2 의 거듭제곱이어야 안전 (polyRef 비트 마스킹 때문).
            // maxPolys=0x8000 은 표준 권장값.
            // =================================================================
            var navMeshParams = new DtNavMeshParams();
            navMeshParams.orig = geom.GetMeshBoundsMin();
            navMeshParams.tileWidth = tileSizeVx * settings.cellSize;
            navMeshParams.tileHeight = tileSizeVx * settings.cellSize;
            navMeshParams.maxTiles = ComputeMaxTiles(geom, tileSizeVx, settings.cellSize);
            navMeshParams.maxPolys = 0x8000;

            var navMesh = new DtNavMesh();
            navMesh.Init(navMeshParams, settings.vertsPerPoly);

            // =================================================================
            // 6. 각 타일 결과를 DtMeshData 로 변환 후 NavMesh 에 등록
            // -----------------------------------------------------------------
            // RcBuilderResult(빌드 산출물) -> DtNavMeshCreateParams(변환 입력) -> DtMeshData(런타임 형식)
            // 변환 흐름. 표준 Recast/Detour API 의 정석 패턴이다.
            // =================================================================
            int totalPolys = 0;
            int addedTiles = 0;
            foreach (var tr in tileResults)
            {
                // 빈 타일 스킵 (빌드 결과 메시가 없는 타일)
                if (tr == null || tr.Mesh == null || tr.Mesh.npolys == 0)
                    continue;

                // 폴리 플래그 설정: 1 = walkable
                // (flags 는 길찾기 필터링용 비트마스크. 여러 종류의 통행 정책을 표현 가능)
                for (int i = 0; i < tr.Mesh.npolys; i++)
                {
                    tr.Mesh.flags[i] = 1;
                }

                // DtNavMeshCreateParams: PolyMesh + PolyMeshDetail + 메타데이터를 묶어 넘기는 구조체.
                // CreateNavMeshData() 가 이걸 받아 길찾기에 최적화된 DtMeshData 로 변환한다.
                var createParams = new DtNavMeshCreateParams();

                // -- PolyMesh 데이터 (다각형 정보) --
                createParams.verts = tr.Mesh.verts;        // 정점 좌표 (정수 격자 단위, cellSize 곱하면 월드 좌표)
                createParams.vertCount = tr.Mesh.nverts;
                createParams.polys = tr.Mesh.polys;        // 폴리곤 정점 인덱스 + 인접 폴리곤 정보
                createParams.polyFlags = tr.Mesh.flags;
                createParams.polyAreas = tr.Mesh.areas;
                createParams.polyCount = tr.Mesh.npolys;
                createParams.nvp = settings.vertsPerPoly;

                // -- Detail Mesh 데이터 (높이 디테일) --
                // detail mesh 가 있으면 길찾기 시 정확한 높이 계산이 가능.
                if (tr.MeshDetail != null)
                {
                    createParams.detailMeshes = tr.MeshDetail.meshes;
                    createParams.detailVerts = tr.MeshDetail.verts;
                    createParams.detailVertsCount = tr.MeshDetail.nverts;
                    createParams.detailTris = tr.MeshDetail.tris;
                    createParams.detailTriCount = tr.MeshDetail.ntris;
                }

                // -- 에이전트/타일 메타데이터 --
                createParams.walkableHeight = settings.agentHeight;
                createParams.walkableRadius = settings.agentRadius;
                createParams.walkableClimb = settings.agentMaxClimb;
                createParams.bmin = tr.Mesh.bmin;         // 타일 바운딩 박스 (월드 좌표)
                createParams.bmax = tr.Mesh.bmax;
                createParams.cs = settings.cellSize;
                createParams.ch = settings.cellHeight;
                createParams.tileX = tr.TileX;           // 타일의 격자 좌표 (정수)
                createParams.tileZ = tr.TileZ;
                createParams.tileLayer = 0;              // 다층 NavMesh (TileCache) 사용 시 의미. 단일 층은 0.
                createParams.buildBvTree = true;         // 타일 내부 BV-tree 빌드 (폴리곤 검색 가속용)

                // 실제 변환. 실패 시 null 반환.
                DtMeshData meshData = DtNavMeshBuilder.CreateNavMeshData(createParams);
                if (meshData == null)
                    continue;

                // NavMesh 에 타일 등록. 이 시점부터 navMesh 안에서 이 타일이 검색 가능해진다.
                // 인자: data, flags(0=기본), lastRef(0=신규), out 결과 ref(여기선 사용 안 함).
                navMesh.AddTile(meshData, 0, 0, out _);
                addedTiles++;
                totalPolys += tr.Mesh.npolys;
            }

            // 단 한 개의 타일도 등록 못 한 경우는 빌드 실패로 간주.
            if (addedTiles == 0)
            {
                result.success = false;
                result.message = "빌드된 타일이 0개 입니다. 씬에 NavMeshSource 가 붙은 walkable 메시가 있는지, 메시가 너무 작거나 가파르지 않은지 확인하세요.";
                return result;
            }

            // =================================================================
            // 7. 출력 경로 결정
            // -----------------------------------------------------------------
            // Application.dataPath 는 Unity 의 "Assets" 폴더 절대 경로.
            // Path.GetFullPath 로 상대경로의 .. 등을 정규화한다.
            // =================================================================
            string assetsDir = Application.dataPath;
            string clientDir = Path.GetFullPath(Path.Combine(assetsDir, settings.clientOutputDir));
            string clientPath = Path.Combine(clientDir, stageName + ".bin");

            string serverPath = null;
            if (settings.writeToServerOutput && !string.IsNullOrEmpty(settings.serverOutputDir))
            {
                string serverDir = Path.GetFullPath(Path.Combine(assetsDir, settings.serverOutputDir));
                serverPath = Path.Combine(serverDir, stageName + ".bin");
            }

            // =================================================================
            // 8. 파일로 저장
            // -----------------------------------------------------------------
            // WriteNavMeshBin 은 DotRecast 의 표준 .bin 포맷으로 저장.
            // 양쪽 경로 모두에 같은 데이터를 쓴다 (서버 자동 동기화).
            //
            // .bin 옆에 .navmeta.json 도 같이 출력한다. 서버는 NavMesh 로드 시 메타를 읽어
            // Stage 의 sector grid 크기(world bounds)를 결정한다.
            // =================================================================
            try
            {
                WriteNavMeshBin(navMesh, clientPath);
                WriteNavMeshMeta(navMesh, clientPath, addedTiles, totalPolys);
                if (serverPath != null)
                {
                    WriteNavMeshBin(navMesh, serverPath);
                    WriteNavMeshMeta(navMesh, serverPath, addedTiles, totalPolys);
                }
            }
            catch (Exception ex)
            {
                result.success = false;
                result.message = $"파일 저장 실패: {ex.Message}";
                return result;
            }

            // 성공 결과 구성
            result.success = true;
            result.message = $"빌드 성공. 타일 {addedTiles}개, 폴리곤 {totalPolys}개";
            result.clientPath = clientPath;
            result.serverPath = serverPath;
            result.tileCount = addedTiles;
            result.totalPolyCount = totalPolys;

            // Unity 가 새로 생긴 파일을 인식하도록 AssetDatabase 새로고침.
            // (StreamingAssets 경로의 파일도 프로젝트 창에 보이게 하기 위함)
            AssetDatabase.Refresh();
            return result;
        }

        // =====================================================================
        // 씬에서 메시 데이터 수집
        // ---------------------------------------------------------------------
        // 활성 씬에서 NavMeshSource 컴포넌트가 붙은 게임오브젝트들을 찾아
        // 그 트리의 MeshFilter 들로부터 정점/삼각형을 모은다.
        //
        // 결과:
        //   vertices[]: x, y, z, x, y, z, ... 순서의 평탄화된 정점 배열 (월드 좌표).
        //   indices[]:  i0, i1, i2, i0, i1, i2, ... 순서의 삼각형 인덱스 배열.
        //   양쪽 모두 Recast 가 기대하는 표준 형식.
        // =====================================================================
        private static bool CollectGeometry(out float[] vertices, out int[] indices, out string message)
        {
            // List 초기 용량을 미리 잡아 GC 압력 감소
            var verts = new List<float>(4096);
            var inds = new List<int>(4096);

            // FindObjectsByType: Unity 6.x 표준 검색 API.
            // FindObjectsInactive.Exclude 는 비활성 오브젝트 제외. (활성 오브젝트만 빌드 대상)
            var sources = UnityEngine.Object.FindObjectsByType<NavMeshSource>(FindObjectsInactive.Exclude);
            if (sources == null || sources.Length == 0)
            {
                vertices = null;
                indices = null;
                message = "씬에 NavMeshSource 컴포넌트가 붙은 게임오브젝트가 없습니다.";
                return false;
            }

            foreach (var src in sources)
            {
                // 한 번 더 활성 상태 확인 (부모가 비활성인 경우 대비)
                if (src == null || !src.gameObject.activeInHierarchy)
                    continue;

                // -- 수집은 Collider 로 일원화 --
                // NavMeshSource 하위의 Collider(Box/Sphere/Capsule/Mesh) 및 TerrainCollider 를 수집한다.
                // 규칙: 비활성 GameObject/Collider 제외, isTrigger 제외, layerMask 로 레이어 필터.
                // 콜라이더 없는 시각 전용 오브젝트(풀/꽃 등)는 자연히 제외된다.
                Collider[] cols;
                if (src.includeChildren)
                {
                    // 두 번째 인자 false = 비활성 GameObject 제외.
                    cols = src.GetComponentsInChildren<Collider>(false);
                }
                else
                {
                    var self = src.GetComponent<Collider>();
                    cols = self != null ? new[] { self } : System.Array.Empty<Collider>();
                }

                foreach (var col in cols)
                {
                    if (col == null || !col.enabled) continue;   // 비활성 콜라이더 제외
                    if (col.isTrigger) continue;                 // 트리거는 물리적 차단이 아니므로 제외
                    // 레이어 필터: layerMask 에 포함된 레이어의 콜라이더만
                    if ((src.layerMask.value & (1 << col.gameObject.layer)) == 0) continue;
                    AppendCollider(col, verts, inds);
                }
            }

            if (verts.Count == 0 || inds.Count == 0)
            {
                vertices = null;
                indices = null;
                message = "NavMeshSource 는 있지만 수집된 지오메트리가 0 입니다. 하위에 (비활성/트리거가 아닌) Collider 나 TerrainCollider 가 있는지, layerMask 가 너무 좁지 않은지 확인하세요.";
                return false;
            }

            vertices = verts.ToArray();
            indices = inds.ToArray();
            message = $"메시 수집 완료: 정점 {verts.Count / 3}, 삼각형 {inds.Count / 3}";
            return true;
        }

        /// <summary>
        /// 한 MeshFilter 의 데이터를 정점/인덱스 누적 버퍼에 추가한다.
        ///
        /// 핵심 포인트:
        /// - 메시는 로컬 좌표로 저장되어 있으므로 Transform.TransformPoint 로 월드 좌표 변환 필수.
        /// - 인덱스를 누적할 때 "지금까지 모인 정점 수" 만큼 오프셋을 더해야 한다.
        ///   (각 메시의 인덱스는 자기 정점 배열 기준 0부터 시작하기 때문)
        /// - 서브메시(머티리얼별로 분리된 삼각형 묶음)도 모두 합쳐서 수집한다.
        /// </summary>
        private static void AppendMesh(Mesh mesh, Transform tr, List<float> verts, List<int> inds)
        {
            if (mesh == null || tr == null)
                return;

            // 현재까지 누적된 정점의 인덱스 시작점. 이 메시의 인덱스에 더해줘야 한다.
            int baseIndex = verts.Count / 3;

            // -- 정점: 로컬 -> 월드 변환 --
            Vector3[] localVerts = mesh.vertices;
            for (int i = 0; i < localVerts.Length; i++)
            {
                Vector3 w = tr.TransformPoint(localVerts[i]);
                verts.Add(w.x);
                verts.Add(w.y);
                verts.Add(w.z);
            }

            // -- 모든 서브메시의 삼각형 인덱스 수집 --
            // submeshCount 는 머티리얼 슬롯 수에 대응. NavMesh 빌드에는 머티리얼 구분이 의미 없으므로 모두 합침.
            for (int sub = 0; sub < mesh.subMeshCount; sub++)
            {
                int[] tris = mesh.GetTriangles(sub);
                for (int i = 0; i < tris.Length; i++)
                    inds.Add(tris[i] + baseIndex);
            }
        }

        /// <summary>
        /// Collider 를 삼각형 지오메트리로 변환해 누적한다.
        /// - MeshCollider: sharedMesh 를 그대로 사용 (가장 정확).
        /// - Box/Sphere/Capsule: 박스로 근사 (nav 장애물 카빙엔 충분하고 빌드가 가볍다).
        /// - TerrainCollider 등 그 외 타입: 스킵 (Terrain 은 AppendTerrain 에서 처리).
        /// </summary>
        private static void AppendCollider(Collider col, List<float> verts, List<int> inds)
        {
            if (col == null)
                return;

            switch (col)
            {
                case MeshCollider mc:
                    if (mc.sharedMesh != null)
                        AppendMesh(mc.sharedMesh, mc.transform, verts, inds);
                    break;

                case BoxCollider bc:
                    AppendLocalBox(bc.transform, bc.center, bc.size, verts, inds);
                    break;

                case SphereCollider sc:
                    // 구 → 지름 정육면체로 근사
                    AppendLocalBox(sc.transform, sc.center, Vector3.one * (sc.radius * 2f), verts, inds);
                    break;

                case CapsuleCollider cc:
                {
                    // 캡슐 → 축 방향 박스로 근사. direction: 0=X, 1=Y, 2=Z.
                    float d = cc.radius * 2f;
                    Vector3 size =
                        cc.direction == 0 ? new Vector3(cc.height, d, d) :
                        cc.direction == 2 ? new Vector3(d, d, cc.height) :
                                            new Vector3(d, cc.height, d);
                    AppendLocalBox(cc.transform, cc.center, size, verts, inds);
                    break;
                }

                case TerrainCollider tc:
                    // 지면. TerrainCollider 도 Collider 의 한 종류로 일원화해서 처리한다.
                    if (tc.terrainData != null)
                        AppendTerrain(tc.terrainData, tc.transform.position, verts, inds);
                    break;
            }
        }

        /// <summary>
        /// 로컬 공간의 박스(center, size)를 월드 삼각형으로 변환해 누적한다.
        /// 8개 코너를 Transform.TransformPoint 로 월드화(스케일/회전 반영)하고 6면 12삼각형을 만든다.
        /// </summary>
        private static void AppendLocalBox(Transform tr, Vector3 center, Vector3 size, List<float> verts, List<int> inds)
        {
            if (tr == null)
                return;

            Vector3 h = size * 0.5f;
            int b = verts.Count / 3;

            // 코너 순서: k = (sx01<<2)|(sy01<<1)|(sz01), sx01∈{0,1}
            for (int sx = 0; sx <= 1; sx++)
                for (int sy = 0; sy <= 1; sy++)
                    for (int sz = 0; sz <= 1; sz++)
                    {
                        Vector3 local = center + new Vector3(
                            (sx == 0 ? -h.x : h.x),
                            (sy == 0 ? -h.y : h.y),
                            (sz == 0 ? -h.z : h.z));
                        Vector3 w = tr.TransformPoint(local);
                        verts.Add(w.x); verts.Add(w.y); verts.Add(w.z);
                    }

            int Idx(int sx, int sy, int sz) => b + ((sx << 2) | (sy << 1) | sz);
            void Quad(int a, int b2, int c2, int d2)
            {
                inds.Add(a); inds.Add(b2); inds.Add(c2);
                inds.Add(a); inds.Add(c2); inds.Add(d2);
            }

            Quad(Idx(0,0,0), Idx(0,1,0), Idx(0,1,1), Idx(0,0,1)); // -X
            Quad(Idx(1,0,0), Idx(1,0,1), Idx(1,1,1), Idx(1,1,0)); // +X
            Quad(Idx(0,0,0), Idx(0,0,1), Idx(1,0,1), Idx(1,0,0)); // -Y
            Quad(Idx(0,1,0), Idx(1,1,0), Idx(1,1,1), Idx(0,1,1)); // +Y
            Quad(Idx(0,0,0), Idx(1,0,0), Idx(1,1,0), Idx(0,1,0)); // -Z
            Quad(Idx(0,0,1), Idx(0,1,1), Idx(1,1,1), Idx(1,0,1)); // +Z
        }

        /// <summary>
        /// Unity Terrain 표면을 res×res 그리드로 샘플링해 삼각형 메시로 누적한다.
        /// 완전 정밀할 필요는 없다(Recast 가 cellSize 로 재래스터화). ~1m 간격이면 충분.
        /// </summary>
        private static void AppendTerrain(TerrainData td, Vector3 pos, List<float> verts, List<int> inds)
        {
            if (td == null)
                return;

            const int res = 129; // 128 셀 = 130m 지형이면 약 1m 간격
            int b = verts.Count / 3;

            for (int zi = 0; zi < res; zi++)
            {
                float v = zi / (float)(res - 1);
                for (int xi = 0; xi < res; xi++)
                {
                    float u = xi / (float)(res - 1);
                    float wy = pos.y + td.GetInterpolatedHeight(u, v);
                    verts.Add(pos.x + u * td.size.x);
                    verts.Add(wy);
                    verts.Add(pos.z + v * td.size.z);
                }
            }

            for (int zi = 0; zi < res - 1; zi++)
            {
                for (int xi = 0; xi < res - 1; xi++)
                {
                    int a = b + zi * res + xi;
                    int r = a + res;
                    // 위(+Y)를 향하는 winding = walkable 지면
                    inds.Add(a); inds.Add(r); inds.Add(a + 1);
                    inds.Add(a + 1); inds.Add(r); inds.Add(r + 1);
                }
            }
        }

        /// <summary>
        /// 맵 크기와 타일 크기로부터 NavMesh 의 maxTiles 를 계산한다.
        ///
        /// 왜 2의 거듭제곱으로 올림하는가:
        /// - DtNavMesh 는 내부적으로 polyRef 를 [salt | tile | poly] 비트 필드로 인코딩한다.
        /// - 각 영역의 비트 수는 maxTiles, maxPolys 값을 기반으로 계산되며,
        ///   2의 거듭제곱일 때 비트 마스킹이 정확히 떨어진다.
        /// - 안 그러면 일부 tile 인덱스가 표현 불가능해질 수 있다.
        /// </summary>
        private static int ComputeMaxTiles(IRcInputGeomProvider geom, int tileSizeVx, float cellSize)
        {
            RcVec3f bmin = geom.GetMeshBoundsMin();
            RcVec3f bmax = geom.GetMeshBoundsMax();

            // 한 타일이 덮는 월드 단위 크기
            float tileWorldSize = tileSizeVx * cellSize;

            // 맵 한 변을 타일로 나눠 올림 (가장자리 타일도 포함)
            int tw = Mathf.CeilToInt((bmax.X - bmin.X) / tileWorldSize);
            int th = Mathf.CeilToInt((bmax.Z - bmin.Z) / tileWorldSize);
            int total = Mathf.Max(1, tw * th);

            // 2의 거듭제곱으로 올림
            int p = 1;
            while (p < total) p <<= 1;

            // 너무 작으면 NavMesh 가 polyRef 비트를 부족하게 할당할 수 있어 최소 4 보장
            return Mathf.Max(p, 4);
        }

        /// <summary>
        /// NavMesh 를 .bin 파일로 저장한다.
        ///
        /// 호환성 메모:
        /// - cCompatibility=true 로 쓰면 RecastDemo 의 saveAll() 과 동일한 헤더 포맷이 된다.
        ///   (magic='MSET', version=1, C struct 패딩 1개 추가)
        /// - 단, DotRecast 의 Writer 는 tileRef 를 64bit 로 쓴다.
        ///   C++ Recast 는 tileRef 가 32bit 이므로 그대로는 호환되지 않을 수 있다.
        ///   현재는 클라 라운드트립 (DotRecast 끼리 읽고 쓰기) 만 지원.
        ///   서버 호환은 Step 2 의 서버 검증 단계에서 확인 후 결정 예정.
        /// </summary>
        private static void WriteNavMeshBin(DtNavMesh navMesh, string path)
        {
            // 출력 디렉터리 자동 생성 (StreamingAssets/NavMesh 등이 없을 수도 있음)
            string dir = Path.GetDirectoryName(path);
            if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir))
                Directory.CreateDirectory(dir);

            // DtMeshSetWriter: NavMesh 전체(모든 타일) 를 단일 .bin 으로 직렬화하는 표준 라이터.
            // using 으로 BinaryWriter/FileStream 의 Dispose 를 보장.
            var writer = new DtMeshSetWriter();
            using var fs = File.Create(path);
            using var bw = new BinaryWriter(fs);
            writer.Write(bw, navMesh, RcByteOrder.LITTLE_ENDIAN, cCompatibility: true);
        }

        // =====================================================================
        // 메타 파일 쓰기
        // ---------------------------------------------------------------------
        // .bin 옆에 나란히 .navmeta.json 을 둔다. 서버는 이 메타를 읽어
        // Stage 의 sector grid 크기(world bounds)를 설정하고,
        // .bin 의 실제 데이터와 대조 검증한다.
        //
        // 포맷 (JSON):
        //   {
        //     "navmesh_file": "<basename>.bin",
        //     "bounds": { "min_x": ..., "min_z": ..., "max_x": ..., "max_z": ... },
        //     "tiles_count": <int>,
        //     "walkable_polygons_count": <int>,
        //     "generated_at": "<ISO8601 UTC>",
        //     "generator_version": "1.0"
        //   }
        //
        // X-Z bounds 는 모든 타일의 bmin/bmax 를 순회해서 계산. (Y 는 서버에서 안 쓰므로 제외)
        // =====================================================================
        private static void WriteNavMeshMeta(DtNavMesh navMesh, string binPath, int tilesCount, int walkablePolygonsCount)
        {
            // 상수: JSON 포맷 버전. 서버가 파싱 로직을 바꿀 때 바꿔야 함.
            const string generatorVersion = "1.0";

            // .bin 파일 이름과 짝이 맞는지 확인할 수 있도록 단순 파일명만 저장 (경로 아님)
            string binFileName = Path.GetFileName(binPath);

            // 모든 타일 순회해서 X-Z AABB 계산.
            // GetTile(i) 는 빈 슬롯도 돌려주므로 data == null 체크 필수.
            double minX = double.MaxValue;
            double minZ = double.MaxValue;
            double maxX = double.MinValue;
            double maxZ = double.MinValue;
            int validTileCount = 0;

            int maxTiles = navMesh.GetMaxTiles();
            for (int i = 0; i < maxTiles; ++i)
            {
                DtMeshTile tile = navMesh.GetTile(i);
                if (tile == null || tile.data == null || tile.data.header == null)
                    continue;

                var h = tile.data.header;
                if (h.bmin.X < minX) minX = h.bmin.X;
                if (h.bmin.Z < minZ) minZ = h.bmin.Z;
                if (h.bmax.X > maxX) maxX = h.bmax.X;
                if (h.bmax.Z > maxZ) maxZ = h.bmax.Z;
                ++validTileCount;
            }

            if (validTileCount == 0)
            {
                // 호출자가 addedTiles > 0 을 확인한 이후에만 이 함수가 닿으면 발생할 이유 없음.
                // 방어적으로 bounds 를 0 으로 세팅해서 쓰고 경고.
                Debug.LogWarning($"NavMeshBuilder.WriteNavMeshMeta: navMesh 에 유효한 tile 이 0 개. bounds 는 0 으로 적힌다.");
                minX = 0; minZ = 0; maxX = 0; maxZ = 0;
            }

            // 수제 JSON. 필드 구조가 고정이라 외부 직렬화 라이브러리 도입은 오버키릴.
            // 일반 부동소수 포맷("G17")으로 제공해 round-trip 안전.
            string nowIso = DateTime.UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ");
            var ci = System.Globalization.CultureInfo.InvariantCulture;
            string json =
                "{\n" +
                $"  \"navmesh_file\": \"{EscapeJsonString(binFileName)}\",\n" +
                "  \"bounds\": {\n" +
                $"    \"min_x\": {minX.ToString("G17", ci)},\n" +
                $"    \"min_z\": {minZ.ToString("G17", ci)},\n" +
                $"    \"max_x\": {maxX.ToString("G17", ci)},\n" +
                $"    \"max_z\": {maxZ.ToString("G17", ci)}\n" +
                "  },\n" +
                $"  \"tiles_count\": {tilesCount},\n" +
                $"  \"walkable_polygons_count\": {walkablePolygonsCount},\n" +
                $"  \"generated_at\": \"{nowIso}\",\n" +
                $"  \"generator_version\": \"{generatorVersion}\"\n" +
                "}\n";

            string metaPath = binPath + ".navmeta.json";

            string dir = Path.GetDirectoryName(metaPath);
            if (!string.IsNullOrEmpty(dir) && !Directory.Exists(dir))
                Directory.CreateDirectory(dir);

            File.WriteAllText(metaPath, json, new System.Text.UTF8Encoding(false));
        }

        /// <summary>JSON 문자열 필드의 최소 이스케이프. 파일명에는 이상한 문자가 거의 없으므로 간단 처리.</summary>
        private static string EscapeJsonString(string s)
        {
            if (string.IsNullOrEmpty(s)) return string.Empty;
            return s.Replace("\\", "\\\\").Replace("\"", "\\\"");
        }
    }
}
