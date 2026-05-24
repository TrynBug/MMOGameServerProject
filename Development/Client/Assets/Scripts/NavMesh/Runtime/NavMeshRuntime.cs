using System;
using System.Collections.Generic;
using System.IO;
using DotRecast.Core.Numerics;
using DotRecast.Detour;
using DotRecast.Detour.Io;
using UnityEngine;

namespace MMO.Client.Navigation
{
    /// <summary>
    /// 한 스테이지의 NavMesh + Query 를 묶어 보관하는 런타임 컨테이너.
    ///
    /// 책임:
    /// - .bin 파일을 로드해서 DtNavMesh 인스턴스 생성
    /// - DtNavMeshQuery 와 IDtQueryFilter 보유
    /// - FindPath / SamplePosition / ClampToNavMesh 같은 게임 코드용 API 제공 (Unity Vector3 인터페이스)
    ///
    /// 사용자는 보통 NavMeshService 를 통해 간접적으로 사용하고, 이 클래스를 직접 만들지는 않는다.
    /// 다만 두 개 이상의 NavMesh 를 동시에 관리하고 싶을 때를 위해 public 으로 노출되어 있다.
    /// </summary>
    public class NavMeshRuntime
    {
        // 외부 API 의 좌표는 Unity Vector3. 내부 DotRecast 호출은 RcVec3f.
        // 두 타입은 메모리 레이아웃이 비슷하지만 직접 캐스팅은 불가하므로 변환 메서드로 다룬다.

        private readonly DtNavMesh m_navMesh;
        private readonly DtNavMeshQuery m_query;
        private readonly IDtQueryFilter m_filter;
        private readonly string m_stageName;

        /// <summary>로드된 스테이지 이름 (디버그 표시 용)</summary>
        public string StageName => m_stageName;

        /// <summary>내부 DtNavMesh (디버그 시각화 등에서 직접 접근 필요 시)</summary>
        public DtNavMesh NavMesh => m_navMesh;

        /// <summary>
        /// NavMesh.bin 파일을 로드한다. 실패 시 예외를 던지지 않고 null 반환.
        /// </summary>
        /// <param name="binPath">절대 경로의 .bin 파일</param>
        /// <param name="stageName">디버그용 스테이지 이름</param>
        public static NavMeshRuntime Load(string binPath, string stageName)
        {
            if (!File.Exists(binPath))
            {
                Debug.LogError($"[NavMeshRuntime] .bin 파일을 찾을 수 없습니다: {binPath}");
                return null;
            }

            try
            {
                var reader = new DtMeshSetReader();
                using var fs = File.OpenRead(binPath);
                using var br = new BinaryReader(fs);
                // DotRecast Writer 로 만든 .bin 은 64bit tileRef -> Read 사용.
                // (서버 C++ 와의 호환은 DT_POLYREF64 매크로로 맞춰져 있다.)
                DtNavMesh navMesh = reader.Read(br, 6);
                if (navMesh == null)
                {
                    Debug.LogError($"[NavMeshRuntime] Read() 가 null 을 반환했습니다: {binPath}");
                    return null;
                }

                return new NavMeshRuntime(navMesh, stageName);
            }
            catch (Exception ex)
            {
                Debug.LogError($"[NavMeshRuntime] 로드 실패 ({binPath}): {ex.Message}\n{ex.StackTrace}");
                return null;
            }
        }

        private NavMeshRuntime(DtNavMesh navMesh, string stageName)
        {
            m_navMesh = navMesh;
            m_query = new DtNavMeshQuery(navMesh);
            // 기본 필터: 모든 area 통과, 일반적인 가중치
            m_filter = new DtQueryDefaultFilter();
            m_stageName = stageName;
        }

        // =====================================================================
        // 길찾기: 시작점에서 끝점까지의 waypoint 리스트
        // =====================================================================

        // 검색 박스 기본 반경. NavMesh 표면에서 살짝 떨어진 점도 가장 가까운 폴리곤을 찾도록 함.
        // y 축이 조금 더 큰 이유는 캐릭터가 살짝 떠 있거나 가라앉아도 NavMesh 위로 보정되게 하기 위함.
        private static readonly RcVec3f sm_findPolyHalfExtents = new RcVec3f(2.0f, 4.0f, 2.0f);

        // 한 번의 길찾기에서 거치는 폴리곤 최대 개수
        private const int MaxPathPolys = 256;
        // 한 번의 길찾기에서 만들어내는 waypoint 최대 개수 (보통 폴리곤 수보다 적거나 비슷)
        private const int MaxStraightPath = 256;

        /// <summary>
        /// 시작점에서 끝점까지의 경로를 계산해 outPath 에 waypoint 들로 채운다.
        ///
        /// 절차 (Recast/Detour 표준):
        ///   1. start/end 위치에서 가장 가까운 폴리곤(start/endRef) 을 찾는다.
        ///   2. FindPath 로 폴리곤 시퀀스(path corridor) 를 구한다.
        ///   3. FindStraightPath 로 폴리곤 시퀀스를 따라 "string pulling" 한 직선 경로(waypoint 리스트) 를 만든다.
        ///
        /// outPath 는 호출자가 미리 생성한 리스트. Clear 후 새 결과를 채운다.
        /// </summary>
        /// <returns>경로를 찾았으면 true, 못 찾았으면 false (outPath 는 비어있음).</returns>
        public bool FindPath(Vector3 start, Vector3 end, List<Vector3> outPath)
        {
            if (outPath == null)
                return false;
            outPath.Clear();

            RcVec3f startRc = ToRc(start);
            RcVec3f endRc = ToRc(end);

            // 1. 시작/끝 폴리곤 찾기
            var status = m_query.FindNearestPoly(startRc, sm_findPolyHalfExtents, m_filter,
                out long startRef, out RcVec3f nearestStart, out _);
            if (status.Failed() || startRef == 0)
                return false;

            status = m_query.FindNearestPoly(endRc, sm_findPolyHalfExtents, m_filter,
                out long endRef, out RcVec3f nearestEnd, out _);
            if (status.Failed() || endRef == 0)
                return false;

            // 2. 폴리곤 시퀀스 (corridor) 찾기
            // Span 으로 받기 위해 스택 배열 사용. MaxPathPolys=256 * 8B = 2KB 정도라 스택 OK.
            Span<long> pathRefs = stackalloc long[MaxPathPolys];
            status = m_query.FindPath(startRef, endRef, nearestStart, nearestEnd, m_filter,
                pathRefs, out int pathCount, MaxPathPolys);
            if (status.Failed() || pathCount == 0)
                return false;

            // 3. 직선 경로 (waypoint) 추출
            Span<DtStraightPath> straight = stackalloc DtStraightPath[MaxStraightPath];
            status = m_query.FindStraightPath(nearestStart, nearestEnd,
                pathRefs.Slice(0, pathCount), pathCount,
                straight, out int straightCount, MaxStraightPath, 0);
            if (status.Failed() || straightCount == 0)
                return false;

            // 결과 채우기
            for (int i = 0; i < straightCount; i++)
            {
                outPath.Add(ToUnity(straight[i].pos));
            }

            return true;
        }

        // =====================================================================
        // 위치 보정: 임의 좌표를 NavMesh 위 가장 가까운 점으로 스냅
        // =====================================================================

        /// <summary>
        /// 임의의 위치를 NavMesh 위 가장 가까운 점으로 보정한다.
        ///
        /// 사용 예:
        /// - 캐릭터 스폰 위치를 NavMesh 위로 강제
        /// - 마우스 클릭 위치(레이캐스트 결과) 가 NavMesh 안쪽에 있는지 검증
        /// - 서버 위치 보정 패킷 처리
        /// </summary>
        /// <param name="input">검사할 위치</param>
        /// <param name="searchRadius">검색 반경 (m). 이 반경 내에서 NavMesh 폴리곤을 찾는다.</param>
        /// <param name="result">NavMesh 위의 보정된 위치 (실패 시 input 그대로)</param>
        /// <returns>NavMesh 위 점을 찾았으면 true</returns>
        public bool SamplePosition(Vector3 input, float searchRadius, out Vector3 result)
        {
            result = input;

            RcVec3f inputRc = ToRc(input);
            RcVec3f halfExtents = new RcVec3f(searchRadius, searchRadius * 2, searchRadius);

            var status = m_query.FindNearestPoly(inputRc, halfExtents, m_filter,
                out long polyRef, out RcVec3f nearest, out _);
            if (status.Failed() || polyRef == 0)
                return false;

            result = ToUnity(nearest);
            return true;
        }

        // =====================================================================
        // 클램프: 마우스 따라가기 용 "갈 수 있는 가장 먼 점"
        // =====================================================================

        // 이분 탐색에서 mid 점이 NavMesh 위인지 확인할 때 쓰는 작은 반경.
        // 너무 작으면 NavMesh 위 점도 "바깥" 으로 오판할 수 있고, 너무 크면 가장자리 너머도 "안쪽" 으로 오판.
        // 0.3 이면 cellSize(0.3) 정도라 폴리곤 한 칸 안쪽인지 명확히 판정.
        private const float ClampProbeRadius = 0.3f;
        // 이분 탐색 반복 횟수. 10회면 (target-from) 거리의 1/1024 정밀도. 100m 입력이어도 10cm 이하 오차.
        private const int ClampBinarySearchIters = 10;

        /// <summary>
        /// 임의 위치를 "from 에서 갈 수 있는 NavMesh 상의 가장 먼 점"으로 클램프한다.
        ///
        /// 마우스 입력이 NavMesh 바깥을 가리킬 때 "일단 갈 수 있는 데까지" 캐릭터가 움직이게 하는 용도.
        /// (클릭 무브 게임의 표준 방식)
        ///
        /// 동작:
        ///   1. target 이 NavMesh 위에 있으면 좌표를 그대로 보정해서 돌려줌.
        ///   2. 아니면 from(NavMesh 위) ~ target(바깥) 사이를 이분 탐색해서 NavMesh 경계 점을 찾음.
        ///   3. from 도 NavMesh 위에 없으면 false (드문 예외 상황).
        ///
        /// 이분 탐색을 쓰는 이유:
        ///   - DotRecast 의 Raycast 는 2D(xz) 투영 기반이라 NavMesh 바깥 끝점일 때 동작이 직관과 다름.
        ///   - 이분 탐색은 SamplePosition (이미 잘 동작하는 API) 만으로 구현 가능하고 결과가 정확함.
        ///   - 비용: SamplePosition 약 10회 호출. 200ms 주기로만 호출되니까 부담 없음.
        /// </summary>
        /// <param name="from">시작점 (보통 캐릭터 현재 위치)</param>
        /// <param name="target">사용자가 가리킨 목표 점 (NavMesh 위가 아닐 수 있음)</param>
        /// <param name="result">클램프된 결과 위치</param>
        /// <returns>성공 시 true</returns>
        public bool ClampToNavMesh(Vector3 from, Vector3 target, out Vector3 result)
        {
            result = from;

            // 1. target 이 NavMesh 위에 있는지 먼저 체크 (일반적인 좋은 경우, 가장 빈번).
            //    기본 반경 (2m) 으로 시도.
            if (SamplePosition(target, sm_findPolyHalfExtents.X, out Vector3 onNav))
            {
                result = onNav;
                return true;
            }

            // 2. target 이 바깥. from 이 NavMesh 위에 있는지 확인하고, 있으면 이분 탐색 시작.
            //    from 자체가 살짝 떠 있을 수도 있어서 좁은 반경으로 한 번 스냅 시도.
            if (!SamplePosition(from, ClampProbeRadius, out Vector3 fromOnNav))
            {
                // from 도 NavMesh 위에 없음. 캐릭터가 NavMesh 밖에 있는 드문 상황. 처리 불가.
                return false;
            }

            // 이분 탐색:
            //   lo = NavMesh 위에 있는 것이 보장된 점 (안쪽)
            //   hi = NavMesh 바깥인 것이 보장된 점 (바깥)
            //   mid 가 NavMesh 위면 lo = mid, 아니면 hi = mid
            //   반복 후 lo 가 "갈 수 있는 가장 먼 점".
            Vector3 lo = fromOnNav;  // NavMesh 위 (확인됨)
            Vector3 hi = target;     // NavMesh 바깥 (1단계에서 확인됨)

            for (int i = 0; i < ClampBinarySearchIters; i++)
            {
                Vector3 mid = (lo + hi) * 0.5f;
                if (SamplePosition(mid, ClampProbeRadius, out Vector3 midOnNav))
                {
                    // mid 가 NavMesh 위. 더 멀리 갈 수 있다. midOnNav 를 새 lo 로
                    // (mid 자체보다 보정된 midOnNav 가 NavMesh 위에 확실히 있으므로 더 안전).
                    lo = midOnNav;
                }
                else
                {
                    // mid 가 NavMesh 바깥. 너무 멀리 갔으므로 hi 를 mid 로.
                    hi = mid;
                }
            }

            // 캐릭터의 y 는 유지하고 싶지만 일단 NavMesh 위 점을 그대로 반환.
            // 호출자(PlayerCharacter)가 y 를 적절히 조정함.
            result = lo;
            return true;
        }

        // =====================================================================
        // 좌표 변환 헬퍼
        // =====================================================================

        // Unity 와 Recast 둘 다 Y-up 오른손 좌표계라 축 변환 없이 그대로 매핑된다.
        private static RcVec3f ToRc(Vector3 v) => new RcVec3f(v.x, v.y, v.z);
        private static Vector3 ToUnity(RcVec3f v) => new Vector3(v.X, v.Y, v.Z);
    }
}
