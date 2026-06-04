#include "pch.h"
#include "Character.h"
#include "GameServer.h"           // buff hooks call SendStatUpdateNtf / SendHpMpNtf
#include "Stage.h"               // SetDestination 에서 Stage::FindPath 호출

#include "Generated/GameData_JobBase.h"   // 생성 시 JobBase 기본스탯 적용

#include <cmath>

namespace
{
    // 같은 위치 판정 임계값 제곱 (X-Z 평면, 유닛^2).
    // 0.1유닛 이내(=10cm) 이면 이동 안 함.
    constexpr float k_minMoveDistSq = 0.01f;

    // 라디안 -> degree 변환 상수
    constexpr float k_radToDeg = 57.2957795f;   // 180.0f / PI

    // waypoint 도달 판정 임계값 제곱 (X-Z 평면, 유닛^2).
    // 0.05유닛 = 5cm. moveDist (5m/s * 50ms = 0.25m) 보다 작아서
    // 도달 판정은 항상 "이번 tick 의 moveDist >= 남은 거리" 로 처리됨.
    // 이 값은 안전망 용도 (부동소수점 오차로 영원히 도달 못 하는 케이스 방지).
    constexpr float k_waypointReachDistSq = 0.0025f;
}

bool Character::Initialize(const DataStructures::Character& protoData)
{
    if (!ActorObject::Initialize(protoData.character_id(), EObjectType::User))
        return false;

    m_protoData = protoData;

    // proto의 좌표/yaw를 부모 StageObject 멤버에 복사.
    // (런타임은 StageObject 멤버를 진실의 원천으로 사용한다.)
    SetPos(protoData.pos_x(), protoData.pos_y(), protoData.pos_z());
    SetYaw(protoData.yaw());

    // 이동 상태는 정지로 시작. dest = 현재 위치.
    m_isMoving = false;
    m_destX    = protoData.pos_x();
    m_destY    = protoData.pos_y();
    m_destZ    = protoData.pos_z();

    // JobBase 게임데이터의 기본스탯을 스탯 컴포넌트에 적용.
    // (향후 레벨/아이템/마스터리/버프는 이어서 각각의 소스가 ApplyStat 한다.)
    if (!applyJobBaseStats())
        return false;

    // 현재 HP/MP 를 최대치로 초기화 (생성 시 풀피).
    // applyJobBaseStats() 이후여야 HpTotal/MpTotal 이 계산되어 있어 최대치가 정해진다.
    FillHp();
    FillMp();

    return true;
}

bool Character::applyJobBaseStats()
{
    const EJob job = static_cast<EJob>(m_protoData.job_id());
    const GameData_JobBase* pJobBase = GameDataTable_JobBase::FindDataByJob(job);
    if (pJobBase == nullptr)
    {
        // JobBase 데이터가 없으면 기본스탯을 적용할 수 없으므로 초기화 실패로 처리한다(false 리턴).
        // 호출자 Character::Initialize 가 false 를 받아 캐릭터 생성을 중단한다.
        LOG_WRITE(LogLevel::Error, std::format("Character::applyJobBaseStats - JobBase not found. objectId={} jobId={}",
            GetObjectId(), m_protoData.job_id()));
        return false;
    }

    // (EStat, value) 쌍 목록을 순회하며 적용. Stat 이 None 인 슬롯은 건너뛴다.
    const int32 statCount = pJobBase->GetStatCount();
    for (int32 i = 0; i < statCount; ++i)
    {
        const EStat stat = pJobBase->GetStat(i);
        if (stat == EStat::None)
            continue;
        m_statComponent.ApplyStat(stat, pJobBase->GetStatValue(i));
    }

    return true;
}

void Character::SyncRuntimeToProto()
{
    // DB 직렬화 직전에 호출되어, 런타임 좌표/yaw를 proto에 반영.
    m_protoData.set_pos_x(GetPosX());
    m_protoData.set_pos_y(GetPosY());
    m_protoData.set_pos_z(GetPosZ());
    m_protoData.set_yaw(GetYaw());
}

void Character::SetDestination(float destX, float destY, float destZ)
{
    // X-Z 평면 거리로 이동 여부 판정. Y(높이) 변화는 이동 트리거에 사용하지 않음.
    const float dx = destX - GetPosX();
    const float dz = destZ - GetPosZ();
    const float distSq = dx * dx + dz * dz;

    if (distSq < k_minMoveDistSq)
    {
        m_isMoving = false;
        m_waypoints.clear();
        m_curWaypointIdx = 0;
        return;
    }

    // 최종 목적지 보관 (MoveNtf 송신용).
    m_destX = destX;
    m_destY = destY;
    m_destZ = destZ;

    // Stage NavMesh 로 waypoint 계산.
    // Stage 가 없거나 FindPath 실패하면 직선 이동 fallback ([목적지] 한 점).
    m_waypoints.clear();
    Stage* pStage = GetStage();
    bool pathFound = false;
    if (pStage)
    {
        pathFound = pStage->FindPath(GetPosX(), GetPosY(), GetPosZ(),
                                     destX, destY, destZ,
                                     m_waypoints);
    }
    if (!pathFound || m_waypoints.size() < 3)
    {
        // 직선 fallback. waypoint = [목적지] 한 점.
        LOG_WRITE(LogLevel::Warn, std::format("Character::SetDestination - FindPath failed, using straight-line fallback. objectId={} from=({},{},{}) to=({},{},{})",
            GetObjectId(),
            GetPosX(), GetPosY(), GetPosZ(),
            destX, destY, destZ));
        m_waypoints.clear();
        m_waypoints.push_back(destX);
        m_waypoints.push_back(destY);
        m_waypoints.push_back(destZ);
    }

    // findStraightPath 의 첫 점은 시작 위치 자체일 수 있음 — 현재 위치와 거의 같으면 스킵.
    // 그 다음 점이 실제로 향해갈 첫 waypoint.
    m_curWaypointIdx = 0;
    while (m_curWaypointIdx * 3 + 2 < static_cast<int32>(m_waypoints.size()))
    {
        const float wx = m_waypoints[m_curWaypointIdx * 3 + 0];
        const float wz = m_waypoints[m_curWaypointIdx * 3 + 2];
        const float wdx = wx - GetPosX();
        const float wdz = wz - GetPosZ();
        if (wdx * wdx + wdz * wdz > k_waypointReachDistSq)
            break;   // 이 waypoint 는 의미 있는 거리, 여기서 멈춤.
        ++m_curWaypointIdx;
    }

    // 모든 waypoint 가 현재 위치와 너무 가까우면 이동 무시.
    if (m_curWaypointIdx * 3 + 2 >= static_cast<int32>(m_waypoints.size()))
    {
        m_isMoving = false;
        m_waypoints.clear();
        m_curWaypointIdx = 0;
        return;
    }

    m_isMoving = true;
    faceCurrentWaypoint();
}

void Character::StopAt(float posX, float posY, float posZ, float yaw)
{
    SetPos(posX, posY, posZ);
    SetYaw(yaw);
    m_destX    = posX;
    m_destY    = posY;
    m_destZ    = posZ;
    m_isMoving = false;
    m_waypoints.clear();
    m_curWaypointIdx = 0;
}

void Character::faceCurrentWaypoint()
{
    // m_curWaypointIdx 가 유효한 인덱스인 상태에서 호출되어야 함.
    // 현재 위치에서 해당 waypoint 까지의 X-Z 평면 방향으로 yaw 갱신.
    const float wx = m_waypoints[m_curWaypointIdx * 3 + 0];
    const float wz = m_waypoints[m_curWaypointIdx * 3 + 2];
    const float dx = wx - GetPosX();
    const float dz = wz - GetPosZ();
    if (dx * dx + dz * dz < k_minMoveDistSq)
        return;   // 너무 가까우면 yaw 유지.

    // Unity 호환: dirY_deg = atan2(dx, dz) * 180/PI (+Z 정면, 시계방향)
    SetYaw(std::atan2(dx, dz) * k_radToDeg);
}

void Character::Update(int64 deltaMs)
{
    if (!m_isMoving)
        return;

    // 안전망: waypoint 가 비었거나 인덱스가 유효 범위 밖이면 정지.
    if (m_waypoints.empty() || m_curWaypointIdx * 3 + 2 >= static_cast<int32>(m_waypoints.size()))
    {
        m_isMoving = false;
        m_waypoints.clear();
        m_curWaypointIdx = 0;
        return;
    }

    // 이번 tick 에 이동할 총 거리.
    const float moveSpeed = static_cast<float>(m_statComponent.Get(EStat::MoveSpdTotal));
    float remainMoveDist = moveSpeed * (static_cast<float>(deltaMs) / 1000.0f);

    // waypoint 들을 따라가면서 remainMoveDist 를 소모. 도달할 때마다 다음 waypoint.
    // 한 tick 안에 여러 waypoint 를 통과할 수 있어서 while 루프.
    while (remainMoveDist > 0.0f)
    {
        const float wx = m_waypoints[m_curWaypointIdx * 3 + 0];
        const float wy = m_waypoints[m_curWaypointIdx * 3 + 1];
        const float wz = m_waypoints[m_curWaypointIdx * 3 + 2];

        const float dx = wx - GetPosX();
        const float dz = wz - GetPosZ();
        const float distSq = dx * dx + dz * dz;
        const float dist = std::sqrt(distSq);

        if (dist <= remainMoveDist)
        {
            // 이 waypoint 에 도달. 정확히 스냅.
            SetPos(wx, wy, wz);
            remainMoveDist -= dist;

            // 다음 waypoint 로 진행.
            ++m_curWaypointIdx;
            if (m_curWaypointIdx * 3 + 2 >= static_cast<int32>(m_waypoints.size()))
            {
                // 마지막 waypoint 도달 → 최종 목적지 도달.
                m_isMoving = false;
                m_waypoints.clear();
                m_curWaypointIdx = 0;
                return;
            }
            // 다음 waypoint 방향으로 yaw 갱신.
            faceCurrentWaypoint();
            // 루프 계속 (남은 remainMoveDist 로 다음 waypoint 향해 진행).
            continue;
        }

        // 이번 tick 안에는 waypoint 에 도달 못 함. 방향으로 remainMoveDist 만큼 이동.
        // Y 는 (waypoint Y - 시작 Y) 를 비례로 보간 (단순 선형).
        const float nx = dx / dist;
        const float nz = dz / dist;
        const float ratio = remainMoveDist / dist;
        const float dy = wy - GetPosY();
        SetPos(GetPosX() + nx * remainMoveDist,
               GetPosY() + dy * ratio,
               GetPosZ() + nz * remainMoveDist);
        remainMoveDist = 0.0f;
    }
}

void Character::OnStatsChangedByBuff()
{
    // Buff added/removed/stacked changed stats -> resend full stat snapshot to the owner client.
    Stage* pStage = GetStage();
    if (pStage == nullptr)
        return;
    GameServer* pServer = pStage->GetGameServer();
    if (pServer == nullptr)
        return;
    pServer->GetPacketSender().SendStatUpdateNtf(GetProto().owner_user_id(), *this);
}

void Character::OnHpChangedByBuff()
{
    // DoT/HoT tick changed current HP -> send current HP/MP to the owner client.
    Stage* pStage = GetStage();
    if (pStage == nullptr)
        return;
    GameServer* pServer = pStage->GetGameServer();
    if (pServer == nullptr)
        return;
    pServer->GetPacketSender().SendHpMpNtf(GetProto().owner_user_id(), GetCurHp(), GetCurMp());
}
