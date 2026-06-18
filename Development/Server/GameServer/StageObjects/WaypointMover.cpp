#include "pch.h"
#include "StageObjects/WaypointMover.h"
#include "StageObjects/StageObject.h"
#include "Stages/Stage.h"

#include <cmath>

namespace
{
    // 같은 위치 판정 임계값 제곱 (X-Z 평면, 유닛^2). 0.1유닛 이내면 이동 안 함.
    constexpr float k_minMoveDistSq = 0.01f;

    // 라디안 -> degree 변환 상수 (180/PI).
    constexpr float k_radToDeg = 57.2957795f;

    // waypoint 도달 판정 임계값 제곱 (X-Z 평면, 유닛^2). 5cm. 부동소수점 안전망.
    constexpr float k_waypointReachDistSq = 0.0025f;
}

void WaypointMover::SetDestination(StageObject& obj, float destX, float destY, float destZ, bool logFallback)
{
    // X-Z 평면 거리로 이동 여부 판정. Y(높이) 변화는 이동 트리거에 사용하지 않음.
    const float dx = destX - obj.GetPosX();
    const float dz = destZ - obj.GetPosZ();
    if (dx * dx + dz * dz < k_minMoveDistSq)
    {
        Stop();
        return;
    }

    // Stage NavMesh 로 waypoint 계산. Stage 가 없거나 FindPath 실패 시 직선 fallback([목적지] 한 점).
    m_waypoints.clear();
    Stage* pStage = obj.GetStage();
    bool pathFound = false;
    if (pStage != nullptr)
        pathFound = pStage->FindPath(obj.GetPosX(), obj.GetPosY(), obj.GetPosZ(), destX, destY, destZ, m_waypoints);

    if (!pathFound || m_waypoints.size() < 3)
    {
        if (logFallback)
        {
            LOG_WRITE(LogLevel::Warn, std::format("FindPath failed, using straight-line fallback. objectId={} from=({},{},{}) to=({},{},{})",
                obj.GetObjectId(),
                obj.GetPosX(), obj.GetPosY(), obj.GetPosZ(),
                destX, destY, destZ));
        }

        m_waypoints.clear();
        m_waypoints.push_back(destX);
        m_waypoints.push_back(destY);
        m_waypoints.push_back(destZ);
    }

    // findStraightPath 의 첫 점은 시작 위치 자체일 수 있음 — 현재 위치와 거의 같은 선두 waypoint 스킵.
    m_curWaypointIdx = 0;
    while (m_curWaypointIdx * 3 + 2 < static_cast<int32>(m_waypoints.size()))
    {
        const float wx = m_waypoints[m_curWaypointIdx * 3 + 0];
        const float wz = m_waypoints[m_curWaypointIdx * 3 + 2];
        const float wdx = wx - obj.GetPosX();
        const float wdz = wz - obj.GetPosZ();
        if (wdx * wdx + wdz * wdz > k_waypointReachDistSq)
            break;   // 이 waypoint 는 의미 있는 거리, 여기서 멈춤.
        ++m_curWaypointIdx;
    }

    // 모든 waypoint 가 현재 위치와 너무 가까우면 이동 무시.
    if (m_curWaypointIdx * 3 + 2 >= static_cast<int32>(m_waypoints.size()))
    {
        Stop();
        return;
    }

    m_isMoving = true;
    faceWaypoint(obj);
}

void WaypointMover::Stop()
{
    m_isMoving = false;
    m_waypoints.clear();
    m_curWaypointIdx = 0;
}

bool WaypointMover::Update(StageObject& obj, int64 deltaMs, float moveSpeed)
{
    if (!m_isMoving)
        return false;

    // 안전망: waypoint 가 비었거나 인덱스가 유효 범위 밖이면 정지.
    if (m_waypoints.empty() || m_curWaypointIdx * 3 + 2 >= static_cast<int32>(m_waypoints.size()))
    {
        Stop();
        return true;
    }

    // 이번 tick 에 이동할 총 거리.
    float remainMoveDist = moveSpeed * (static_cast<float>(deltaMs) / 1000.0f);

    // waypoint 들을 따라가면서 remainMoveDist 를 소모. 한 tick 안에 여러 waypoint 통과 가능 → while 루프.
    while (remainMoveDist > 0.0f)
    {
        const float wx = m_waypoints[m_curWaypointIdx * 3 + 0];
        const float wy = m_waypoints[m_curWaypointIdx * 3 + 1];
        const float wz = m_waypoints[m_curWaypointIdx * 3 + 2];

        const float dx = wx - obj.GetPosX();
        const float dz = wz - obj.GetPosZ();
        const float dist = std::sqrt(dx * dx + dz * dz);

        // 현재 waypoint 가 경로의 마지막(= 최종 목적지)인지.
        const bool isLastWaypoint = (m_curWaypointIdx * 3 + 5 >= static_cast<int32>(m_waypoints.size()));

        if (dist <= remainMoveDist)
        {
            if (isLastWaypoint)
            {
                // 최종 목적지 도달: 정확히 스냅하고 정지(자투리<1tick 는 클라 보간에 안 보임).
                obj.SetPos(wx, wy, wz);
                Stop();
                return true;
            }

            // 중간 waypoint: 스냅 후 자투리 거리로 다음 구간을 이어서 이동(연속, 점프 없음).
            obj.SetPos(wx, wy, wz);
            remainMoveDist -= dist;
            ++m_curWaypointIdx;
            faceWaypoint(obj);
            continue;
        }

        // 이번 tick 안에는 waypoint 도달 못 함. 방향으로 remainMoveDist 만큼 이동. Y 는 비례 보간.
        const float nx = dx / dist;
        const float nz = dz / dist;
        const float ratio = remainMoveDist / dist;
        const float dy = wy - obj.GetPosY();
        obj.SetPos(obj.GetPosX() + nx * remainMoveDist,
                   obj.GetPosY() + dy * ratio,
                   obj.GetPosZ() + nz * remainMoveDist);
        remainMoveDist = 0.0f;
    }

    return false;
}

std::span<const float> WaypointMover::GetRemainingWaypoints() const
{
    if (!m_isMoving)
        return {};
    const int32 start = m_curWaypointIdx * 3;
    if (start < 0 || start >= static_cast<int32>(m_waypoints.size()))
        return {};
    return std::span<const float>(m_waypoints.data() + start, m_waypoints.size() - static_cast<size_t>(start));
}

void WaypointMover::faceWaypoint(StageObject& obj)
{
    const float wx = m_waypoints[m_curWaypointIdx * 3 + 0];
    const float wz = m_waypoints[m_curWaypointIdx * 3 + 2];
    const float dx = wx - obj.GetPosX();
    const float dz = wz - obj.GetPosZ();
    if (dx * dx + dz * dz < k_minMoveDistSq)
        return;   // 너무 가까우면 yaw 유지.
    // Unity 호환: dirY_deg = atan2(dx, dz) * 180/PI (+Z 정면, 시계방향)
    obj.SetYaw(std::atan2(dx, dz) * k_radToDeg);
}
