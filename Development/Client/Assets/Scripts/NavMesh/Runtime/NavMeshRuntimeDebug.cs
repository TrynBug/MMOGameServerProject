using DotRecast.Detour;
using UnityEngine;

namespace MMO.Client.Navigation
{
    /// <summary>
    /// 런타임에 NavMesh 와 경로를 디버그 그리기로 보여주는 헬퍼.
    ///
    /// Editor 한정의 NavMeshVisualizer 와 다르게 이건 Game 뷰에서도 보인다.
    /// Debug.DrawLine 은 GameView 의 "Gizmos" 토글이 켜져 있어야 보이는 점에 주의.
    ///
    /// 사용 예:
    ///   void Update() {
    ///       NavMeshRuntimeDebug.DrawNavMesh(NavMeshService.Current, Color.cyan);
    ///       NavMeshRuntimeDebug.DrawPath(myPath, Color.yellow);
    ///   }
    ///
    /// 매 프레임 호출되도록 Update 등에서 부르면 된다 (Debug.DrawLine 은 1 프레임만 유지됨).
    /// </summary>
    public static class NavMeshRuntimeDebug
    {
        /// <summary>
        /// 로드된 NavMesh 의 모든 폴리곤 가장자리를 그린다.
        /// </summary>
        /// <param name="runtime">대상 NavMeshRuntime. null 이면 아무것도 안 그림.</param>
        /// <param name="color">선 색상</param>
        /// <param name="yOffset">지면 위로 살짝 띄우는 값 (z-fighting 방지)</param>
        public static void DrawNavMesh(NavMeshRuntime runtime, Color color, float yOffset = 0.05f)
        {
            if (runtime == null || runtime.NavMesh == null)
                return;

            DtNavMesh navMesh = runtime.NavMesh;
            int maxTiles = navMesh.GetMaxTiles();
            for (int i = 0; i < maxTiles; i++)
            {
                DtMeshTile tile = navMesh.GetTile(i);
                if (tile == null || tile.data == null || tile.data.header == null)
                    continue;

                DrawTile(tile, color, yOffset);
            }
        }

        /// <summary>
        /// waypoint 리스트를 선분으로 연결해 그린다.
        /// </summary>
        public static void DrawPath(System.Collections.Generic.IList<Vector3> path, Color color, float yOffset = 0.1f)
        {
            if (path == null || path.Count < 2)
                return;

            Vector3 offset = new Vector3(0, yOffset, 0);
            for (int i = 0; i < path.Count - 1; i++)
            {
                Debug.DrawLine(path[i] + offset, path[i + 1] + offset, color);
            }
        }

        // ---------------------------------------------------------------------
        // 내부: 한 타일의 모든 폴리곤 그리기
        // ---------------------------------------------------------------------
        // Editor 의 NavMeshVisualizer 와 동일한 구조지만 Handles 대신 Debug.DrawLine 사용.
        // Debug.DrawLine 은 1 프레임만 유지되므로 호출자가 매 프레임 호출해야 한다.
        private static void DrawTile(DtMeshTile tile, Color color, float yOffset)
        {
            var data = tile.data;
            var header = data.header;

            for (int p = 0; p < header.polyCount; p++)
            {
                var poly = data.polys[p];
                // off-mesh connection 은 면이 아니라 점-점 링크라 스킵
                if (poly.GetPolyType() == DtPolyTypes.DT_POLYTYPE_OFFMESH_CONNECTION)
                    continue;

                int vc = poly.vertCount;
                if (vc < 3)
                    continue;

                // 폴리곤 정점들을 Unity 좌표로 변환해서 모음
                Vector3[] pts = new Vector3[vc];
                for (int j = 0; j < vc; j++)
                {
                    int vi = poly.verts[j] * 3;
                    pts[j] = new Vector3(data.verts[vi], data.verts[vi + 1] + yOffset, data.verts[vi + 2]);
                }

                // 가장자리 그리기. 마지막 정점에서 첫 정점으로 닫는다.
                for (int j = 0; j < vc; j++)
                {
                    Debug.DrawLine(pts[j], pts[(j + 1) % vc], color);
                }
            }
        }
    }
}
