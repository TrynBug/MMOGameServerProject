#include "pch.h"
#include "StageNavMesh.h"

// Detour
#include "DetourNavMesh.h"
#include "DetourNavMeshQuery.h"

#include <random>

StageNavMesh::StageNavMesh(const dtNavMesh* pNavMesh)
    : m_pNavMesh(pNavMesh)
{
    if (!pNavMesh)
    {
        // NavMesh 없는 Stage (예: SystemStage). 정상 케이스.
        return;
    }

    dtNavMeshQuery* pQuery = dtAllocNavMeshQuery();
    if (!pQuery)
    {
        LOG_WRITE(LogLevel::Error, "dtAllocNavMeshQuery failed.");
        m_pNavMesh = nullptr;
        return;
    }

    // maxNodes 는 한 번의 path query 에서 사용할 최대 탐색 노드 수.
    // Recast 공식 데모 기본값이 2048 이라 그대로 사용.
    const dtStatus status = pQuery->init(pNavMesh, 2048);
    if (dtStatusFailed(status))
    {
        LOG_WRITE(LogLevel::Error, std::format("dtNavMeshQuery::init failed. status=0x{:x}", static_cast<uint32>(status)));
        dtFreeNavMeshQuery(pQuery);
        m_pNavMesh = nullptr;
        return;
    }

    m_pNavQuery = pQuery;
    m_pNavFilter = new dtQueryFilter();   // 기본 필터 (모든 area 통과, 일반 가중치)
}

StageNavMesh::~StageNavMesh()
{
    // NavMesh 자체는 NavMeshManager 가 소유하므로 여기서 해제하지 않는다.
    if (m_pNavQuery)
    {
        dtFreeNavMeshQuery(m_pNavQuery);
        m_pNavQuery = nullptr;
    }
    if (m_pNavFilter)
    {
        delete m_pNavFilter;
        m_pNavFilter = nullptr;
    }
}

bool StageNavMesh::FindPath(float startX, float startY, float startZ,
                            float endX,   float endY,   float endZ,
                            std::vector<float>& outWaypoints) const
{
    outWaypoints.clear();

    if (!IsReady())
    {
        LOG_WRITE(LogLevel::Warn, "NavMeshQuery not ready.");
        return false;
    }

    // 검색 박스 반경. 클라 NavMeshRuntime 와 동일 (X/Z=2, Y=4).
    // Y가 커야 캐릭터가 살짝 떠 있거나 가라앉아도 가까운 NavMesh 폴리곤 찾음.
    const float halfExtents[3] = { 2.0f, 4.0f, 2.0f };
    const float startPos[3] = { startX, startY, startZ };
    const float endPos[3]   = { endX,   endY,   endZ   };

    dtPolyRef startRef = 0;
    dtPolyRef endRef   = 0;
    float     nearestStart[3] = { 0, 0, 0 };
    float     nearestEnd[3]   = { 0, 0, 0 };

    dtStatus status = m_pNavQuery->findNearestPoly(startPos, halfExtents, m_pNavFilter, &startRef, nearestStart);
    if (dtStatusFailed(status) || startRef == 0)
    {
        LOG_WRITE(LogLevel::Warn, std::format("findNearestPoly(start) failed. start=({},{},{})", startX, startY, startZ));
        return false;
    }

    status = m_pNavQuery->findNearestPoly(endPos, halfExtents, m_pNavFilter, &endRef, nearestEnd);
    if (dtStatusFailed(status) || endRef == 0)
    {
        LOG_WRITE(LogLevel::Warn, std::format("findNearestPoly(end) failed. end=({},{},{})", endX, endY, endZ));
        return false;
    }

    // 폴리곤 시퀀스 (corridor) 찾기
    constexpr int k_maxPathPolys = 256;
    dtPolyRef pathRefs[k_maxPathPolys];
    int pathCount = 0;

    status = m_pNavQuery->findPath(startRef, endRef, nearestStart, nearestEnd, m_pNavFilter, pathRefs, &pathCount, k_maxPathPolys);
    if (dtStatusFailed(status) || pathCount == 0)
    {
        LOG_WRITE(LogLevel::Warn, std::format("findPath failed. status=0x{:x} pathCount={}", static_cast<uint32>(status), pathCount));
        return false;
    }

    // 직선 경로 (waypoint) 추출
    constexpr int k_maxStraightPath = 256;
    float         straightPath[k_maxStraightPath * 3];   // (x, y, z) * N
    unsigned char straightPathFlags[k_maxStraightPath];
    dtPolyRef     straightPathRefs[k_maxStraightPath];
    int           straightCount = 0;

    status = m_pNavQuery->findStraightPath(nearestStart, nearestEnd, pathRefs, pathCount,
        straightPath, straightPathFlags, straightPathRefs, &straightCount, k_maxStraightPath, 0);
    if (dtStatusFailed(status) || straightCount == 0)
    {
        LOG_WRITE(LogLevel::Warn, std::format("findStraightPath failed. status=0x{:x} straightCount={}",
            static_cast<uint32>(status), straightCount));
        return false;
    }

    // 결과 채우기
    outWaypoints.reserve(straightCount * 3);
    for (int i = 0; i < straightCount * 3; ++i)
    {
        outWaypoints.push_back(straightPath[i]);
    }
    return true;
}

bool StageNavMesh::SamplePosition(float x, float y, float z,
                                  float halfExtentX, float halfExtentY, float halfExtentZ,
                                  float& outX, float& outY, float& outZ) const
{
    if (!IsReady())
        return false;

    const float center[3]      = { x, y, z };
    const float halfExtents[3] = { halfExtentX, halfExtentY, halfExtentZ };

    dtPolyRef nearestRef   = 0;
    float     nearestPt[3] = { 0, 0, 0 };

    const dtStatus status = m_pNavQuery->findNearestPoly(center, halfExtents, m_pNavFilter, &nearestRef, nearestPt);
    if (dtStatusFailed(status) || nearestRef == 0)
        return false;

    outX = nearestPt[0];
    outY = nearestPt[1];
    outZ = nearestPt[2];
    return true;
}

namespace
{
    // findRandomPointAroundCircle 용 [0,1) 난수. 컨텐츠 스레드별 독립 RNG (rand() 전역상태 경쟁 회피).
    float navFrand()
    {
        static thread_local std::mt19937 rng(std::random_device{}());
        static thread_local std::uniform_real_distribution<float> dist(0.f, 1.f);
        return dist(rng);
    }
}

bool StageNavMesh::SampleRandomPoint(float cx, float cy, float cz, float radius,
                                     float& outX, float& outY, float& outZ) const
{
    if (!IsReady())
        return false;

    // 1) center 근처의 시작 폴리곤을 찾는다 (Y 불확실 → Y 검색 반경 넉넉히).
    const float center[3]      = { cx, cy, cz };
    const float searchXZ       = (radius > 2.f) ? radius : 2.f;
    const float halfExtents[3] = { searchXZ, 1000.f, searchXZ };

    dtPolyRef startRef   = 0;
    float     startPt[3] = { 0, 0, 0 };
    dtStatus  status = m_pNavQuery->findNearestPoly(center, halfExtents, m_pNavFilter, &startRef, startPt);
    if (dtStatusFailed(status) || startRef == 0)
        return false;

    // 2) 시작 폴리곤 기준 반경 내 walkable 랜덤 좌표.
    dtPolyRef randomRef   = 0;
    float     randomPt[3] = { 0, 0, 0 };
    status = m_pNavQuery->findRandomPointAroundCircle(startRef, startPt, radius, m_pNavFilter, navFrand, &randomRef, randomPt);
    if (dtStatusFailed(status) || randomRef == 0)
        return false;

    outX = randomPt[0];
    outY = randomPt[1];
    outZ = randomPt[2];
    return true;
}
