using System;
using System.Collections.Generic;
using System.Numerics;
using DotRecast.Core.Numerics;
using DotRecast.Detour;

namespace DummyClient.Nav
{
    // 클라의 NavMeshRuntime.cs 를 헤드리스로 이식.
    // 차이: UnityEngine.Vector3 → System.Numerics.Vector3, Debug.Log 제거.
    // DotRecast 호출부(FindNearestPoly/FindPath/FindStraightPath)는 원본과 동일.
    //
    // DtNavMesh(불변)는 봇들이 공유하지만(NavMeshCache), DtNavMeshQuery 는 내부 노드풀이
    // 가변이라 NavField(=쿼리) 인스턴스는 봇마다 하나씩 갖는다.
    public sealed class NavField
    {
        private readonly DtNavMeshQuery m_query;
        private readonly IDtQueryFilter m_filter;

        private static readonly RcVec3f FindPolyHalfExtents = new RcVec3f(2.0f, 4.0f, 2.0f);
        private const int MaxPathPolys = 256;
        private const int MaxStraightPath = 256;

        public NavField(DtNavMesh navMesh)
        {
            m_query = new DtNavMeshQuery(navMesh);
            m_filter = new DtQueryDefaultFilter();
        }

        // 시작→끝 경로를 waypoint 리스트로 채운다. 못 찾으면 false.
        public bool FindPath(Vector3 start, Vector3 end, List<Vector3> outPath)
        {
            if (outPath == null) return false;
            outPath.Clear();

            var status = m_query.FindNearestPoly(ToRc(start), FindPolyHalfExtents, m_filter,
                out long startRef, out RcVec3f nearestStart, out _);
            if (status.Failed() || startRef == 0) return false;

            status = m_query.FindNearestPoly(ToRc(end), FindPolyHalfExtents, m_filter,
                out long endRef, out RcVec3f nearestEnd, out _);
            if (status.Failed() || endRef == 0) return false;

            Span<long> pathRefs = stackalloc long[MaxPathPolys];
            status = m_query.FindPath(startRef, endRef, nearestStart, nearestEnd, m_filter,
                pathRefs, out int pathCount, MaxPathPolys);
            if (status.Failed() || pathCount == 0) return false;

            Span<DtStraightPath> straight = stackalloc DtStraightPath[MaxStraightPath];
            status = m_query.FindStraightPath(nearestStart, nearestEnd,
                pathRefs.Slice(0, pathCount), pathCount,
                straight, out int straightCount, MaxStraightPath, 0);
            if (status.Failed() || straightCount == 0) return false;

            for (int i = 0; i < straightCount; i++)
                outPath.Add(ToVec(straight[i].pos));

            return outPath.Count > 0;
        }

        // 임의 위치를 NavMesh 위 가장 가까운 점으로 스냅. 실패 시 result=input.
        public bool SamplePosition(Vector3 input, float searchRadius, out Vector3 result)
        {
            result = input;
            var halfExtents = new RcVec3f(searchRadius, searchRadius * 2f, searchRadius);
            var status = m_query.FindNearestPoly(ToRc(input), halfExtents, m_filter,
                out long polyRef, out RcVec3f nearest, out _);
            if (status.Failed() || polyRef == 0) return false;
            result = ToVec(nearest);
            return true;
        }

        private static RcVec3f ToRc(Vector3 v) => new RcVec3f(v.X, v.Y, v.Z);
        private static Vector3 ToVec(RcVec3f v) => new Vector3(v.X, v.Y, v.Z);
    }
}
