#include "pch.h"
#include "Monster.h"
#include "Stage.h"      // 타겟 탐색(ForEachAdjacentSector)/길찾기(FindPath)/sector 갱신/FindObject
#include "Sector.h"     // 타겟 탐색 시 GetUsers

#include <cmath>

namespace
{
    // ── 이동 임계값 (Character.cpp 와 동일 규약) ──
    constexpr float k_minMoveDistSq       = 0.01f;     // 0.1유닛 이내면 이동 안 함
    constexpr float k_radToDeg            = 57.2957795f;
    constexpr float k_waypointReachDistSq = 0.0025f;   // 5cm. 부동소수점 안전망.

    // throttled repath: 목표점이 1유닛 이상 움직이면 재길찾기.
    constexpr float k_repathDistSq = 1.0f;

    // 사망 후 시체 유지 시간(ms). 이 시간이 지나면 디스폰한다.
    constexpr int64 k_corpseDurationMs = 30000;
}

bool Monster::Initialize(int64 objectId, const GameData_Monster* pMonsterData)
{
    if (pMonsterData == nullptr)
    {
        LOG_WRITE(LogLevel::Error, std::format("Monster::Initialize - monster data is null. objectId={}", objectId));
        return false;
    }
    if (!ActorObject::Initialize(objectId, EObjectType::Monster))
        return false;

    m_pMonsterData = pMonsterData;

    // 종류 데이터의 기본스탯을 스탯 컴포넌트에 적용.
    if (!applyBaseStats())
        return false;

    // 현재 HP/MP 를 최대치로 초기화 (스폰 시 풀피).
    FillHp();
    FillMp();

    // TODO(데이터): 스킬을 GameData_Monster 에서 읽어 채운다. 지금은 뼈대 검증용 하드코딩.
    //   기본 공격(선딜 없음, 짧은 쿨다운) + 시전 스킬(선딜 800ms, 긴 쿨다운) 예시.
    MonsterSkill basicAttack;
    basicAttack.skillId    = 1;
    basicAttack.range      = m_attackRange;
    basicAttack.cooldownMs = 1500;
    basicAttack.castTimeMs = 0;
    m_skills.push_back(basicAttack);

    MonsterSkill castSkill;
    castSkill.skillId    = 2;
    castSkill.range      = m_attackRange;
    castSkill.cooldownMs = 5000;
    castSkill.castTimeMs = 800;
    m_skills.push_back(castSkill);

    return true;
}

bool Monster::applyBaseStats()
{
    if (m_pMonsterData == nullptr)
    {
        LOG_WRITE(LogLevel::Error, std::format("Monster::applyBaseStats - monster data is null. objectId={}", GetObjectId()));
        return false;
    }

    // (EStat, value) 쌍 목록을 순회하며 적용. Stat 이 None 인 슬롯은 건너뛴다.
    // (JobBase 와 동일 패턴. 3번째 소비처가 생기면 템플릿 헬퍼로 통합 예정.)
    const int32 statCount = m_pMonsterData->GetStatCount();
    for (int32 i = 0; i < statCount; ++i)
    {
        const EStat stat = m_pMonsterData->GetStat(i);
        if (stat == EStat::None)
            continue;
        m_statComponent.ApplyStat(stat, m_pMonsterData->GetStatValue(i));
    }

    return true;
}

bool Monster::AdvanceCorpseTimer(int64 deltaMs)
{
    m_corpseElapsedMs += deltaMs;
    return m_corpseElapsedMs >= k_corpseDurationMs;
}

// ─────────────────────────────────────────────────────────────
// Update: 공통 housekeeping 후 두뇌에 위임 (FSM/BT 등 교체 가능)
// ─────────────────────────────────────────────────────────────
void Monster::Update(int64 deltaMs)
{
    if (GetStage() == nullptr)
        return;   // 아직 Stage 에 등록되지 않음.

    // 스폰 지점 1회 캡처 (복귀 기준점). 첫 Update 위치 = 스폰 위치.
    if (!m_spawnPointSet)
    {
        m_spawnX = GetPosX();
        m_spawnY = GetPosY();
        m_spawnZ = GetPosZ();
        m_spawnPointSet = true;
    }

    // 스킬 쿨다운 진행 (행위 주체 상태이므로 두뇌 종류와 무관하게 여기서 처리).
    tickSkillCooldowns(deltaMs);

    // 의사결정/행동은 두뇌에 위임.
    if (m_ai)
        m_ai->Update(*this, deltaMs);
}

void Monster::tickSkillCooldowns(int64 deltaMs)
{
    for (MonsterSkill& skill : m_skills)
    {
        if (skill.remainingCooldownMs > 0)
        {
            skill.remainingCooldownMs -= deltaMs;
            if (skill.remainingCooldownMs < 0)
                skill.remainingCooldownMs = 0;
        }
    }
}

// ─────────────────────────────────────────────────────────────
// 공유 행동 레이어 — 타겟
// ─────────────────────────────────────────────────────────────
void Monster::AcquireTarget()
{
    Stage* pStage = GetStage();
    if (pStage == nullptr)
        return;

    // 어그로 범위를 덮는 sector 범위 계산 (최소 1).
    const double sectorSize = pStage->GetSectorSize();
    int32 sectorRange = 1;
    if (sectorSize > 0.0)
    {
        sectorRange = static_cast<int32>(std::ceil(m_aggroRange / sectorSize));
        if (sectorRange < 1)
            sectorRange = 1;
    }

    const float myX = GetPosX();
    const float myZ = GetPosZ();

    int64 bestId = 0;
    float bestSq = m_aggroRange * m_aggroRange;   // 이 값 이하인 유저만 후보.

    pStage->ForEachAdjacentSector(GetCurSectorX(), GetCurSectorZ(), sectorRange,
        [&](Sector* pSector)
        {
            for (const auto& pair : pSector->GetUsers())
            {
                const StageObject* pUser = pair.second;
                const float udx = pUser->GetPosX() - myX;
                const float udz = pUser->GetPosZ() - myZ;
                const float dsq = udx * udx + udz * udz;
                if (dsq <= bestSq)
                {
                    bestSq = dsq;
                    bestId = pUser->GetObjectId();
                }
            }
        });

    m_targetObjectId = bestId;
}

StageObject* Monster::GetTarget() const
{
    if (m_targetObjectId == 0)
        return nullptr;
    Stage* pStage = GetStage();
    if (pStage == nullptr)
        return nullptr;
    return pStage->FindObject(m_targetObjectId);   // 사라졌으면 nullptr.
}

void Monster::FaceTarget(const StageObject* pTarget)
{
    if (pTarget == nullptr)
        return;
    const float dx = pTarget->GetPosX() - GetPosX();
    const float dz = pTarget->GetPosZ() - GetPosZ();
    if (dx * dx + dz * dz < k_minMoveDistSq)
        return;
    // Unity 호환: yaw_deg = atan2(dx, dz) * 180/PI (+Z 정면).
    SetYaw(std::atan2(dx, dz) * k_radToDeg);
}

// ─────────────────────────────────────────────────────────────
// 공유 행동 레이어 — 스킬
// ─────────────────────────────────────────────────────────────
int32 Monster::SelectReadySkill(float distToTarget) const
{
    // 목록 순서 = 우선순위. 쿨다운 끝 + 사거리 내 첫 스킬을 고른다.
    // (가장 가벼운 형태의 상황별 선택. 가중치/조건이 필요하면 여기서 확장.)
    for (size_t i = 0; i < m_skills.size(); ++i)
    {
        const MonsterSkill& skill = m_skills[i];
        if (skill.remainingCooldownMs <= 0 && distToTarget <= skill.range)
            return static_cast<int32>(i);
    }
    return -1;
}

void Monster::StartSkillCooldown(int32 index)
{
    if (index < 0 || index >= static_cast<int32>(m_skills.size()))
        return;
    m_skills[index].remainingCooldownMs = m_skills[index].cooldownMs;
}

void Monster::ExecuteSkill(int32 index, StageObject* pTarget)
{
    if (index < 0 || index >= static_cast<int32>(m_skills.size()))
        return;

    // TODO(전투): 실제 스킬 효과 구현.
    //   - 스킬 데이터(데미지 계수/판정 범위/투사체 등) 조회
    //   - 근접: 즉시 판정 → 대상 ActorObject 에 데미지 적용
    //   - 원거리: 투사체 생성 + 클라 통보
    //   전투/스킬 시스템이 아직 없으므로 현재는 뼈대 검증용 로그만 남긴다.
    const MonsterSkill& skill = m_skills[index];
    LOG_WRITE(LogLevel::Info, std::format("Monster::ExecuteSkill(STUB) - objectId={} skillId={} targetId={}",
        GetObjectId(), skill.skillId, (pTarget != nullptr ? pTarget->GetObjectId() : 0)));
}

// ─────────────────────────────────────────────────────────────
// 공유 행동 레이어 — 이동 (Character 패턴 미러링, 이동속도만 m_moveSpeed 사용)
// ─────────────────────────────────────────────────────────────
void Monster::MoveTo(float destX, float destY, float destZ, int64 deltaMs)
{
    // throttled repath: 목표점이 충분히 바뀌었거나 정지 상태면 새 경로 계산.
    const float pdx = destX - m_pathTargetX;
    const float pdz = destZ - m_pathTargetZ;
    if (!m_isMoving || !m_hasPathTarget || (pdx * pdx + pdz * pdz) > k_repathDistSq)
    {
        setDestination(destX, destY, destZ);
        m_pathTargetX  = destX;
        m_pathTargetZ  = destZ;
        m_hasPathTarget = true;
    }

    updateMovement(deltaMs);

    if (Stage* pStage = GetStage())
        pStage->UpdateObjectSector(this);

    // TODO(통신): 몬스터 이동을 주변 유저에게 MoveNtf 로 브로드캐스트해야 클라가 움직임을 본다.
    //   (현재는 서버 권위 위치만 갱신. Character 의 broadcastMoveNtf 에 대응하는 몬스터 경로 필요.)
}

void Monster::SnapToSpawn()
{
    SetPos(m_spawnX, m_spawnY, m_spawnZ);
    if (Stage* pStage = GetStage())
        pStage->UpdateObjectSector(this);
}

void Monster::StopMoving()
{
    if (m_isMoving)
        m_moveStateDirty = true;   // 이동 -> 정지 상태 변화.
    m_isMoving = false;
    m_waypoints.clear();
    m_curWaypointIdx = 0;
    m_hasPathTarget = false;
}

void Monster::setDestination(float destX, float destY, float destZ)
{
    const float dx = destX - GetPosX();
    const float dz = destZ - GetPosZ();
    if (dx * dx + dz * dz < k_minMoveDistSq)
    {
        StopMoving();
        return;
    }

    m_destX = destX;
    m_destY = destY;
    m_destZ = destZ;

    // Stage NavMesh 로 waypoint 계산. 실패 시 직선 fallback.
    m_waypoints.clear();
    Stage* pStage = GetStage();
    bool pathFound = false;
    if (pStage != nullptr)
        pathFound = pStage->FindPath(GetPosX(), GetPosY(), GetPosZ(), destX, destY, destZ, m_waypoints);

    if (!pathFound || m_waypoints.size() < 3)
    {
        m_waypoints.clear();
        m_waypoints.push_back(destX);
        m_waypoints.push_back(destY);
        m_waypoints.push_back(destZ);
    }

    // 현재 위치와 거의 같은 선두 waypoint 스킵.
    m_curWaypointIdx = 0;
    while (m_curWaypointIdx * 3 + 2 < static_cast<int32>(m_waypoints.size()))
    {
        const float wx = m_waypoints[m_curWaypointIdx * 3 + 0];
        const float wz = m_waypoints[m_curWaypointIdx * 3 + 2];
        const float wdx = wx - GetPosX();
        const float wdz = wz - GetPosZ();
        if (wdx * wdx + wdz * wdz > k_waypointReachDistSq)
            break;
        ++m_curWaypointIdx;
    }
    if (m_curWaypointIdx * 3 + 2 >= static_cast<int32>(m_waypoints.size()))
    {
        StopMoving();
        return;
    }

    m_isMoving = true;
    m_moveStateDirty = true;   // 새 목적지로 이동 시작/갱신 — 상태 변화.
    faceWaypoint();
}

bool Monster::updateMovement(int64 deltaMs)
{
    if (!m_isMoving)
        return false;

    if (m_waypoints.empty() || m_curWaypointIdx * 3 + 2 >= static_cast<int32>(m_waypoints.size()))
    {
        StopMoving();
        return true;
    }

    float remainMoveDist = m_moveSpeed * (static_cast<float>(deltaMs) / 1000.0f);

    // 한 tick 안에 여러 waypoint 를 통과할 수 있어 while 루프.
    while (remainMoveDist > 0.0f)
    {
        const float wx = m_waypoints[m_curWaypointIdx * 3 + 0];
        const float wy = m_waypoints[m_curWaypointIdx * 3 + 1];
        const float wz = m_waypoints[m_curWaypointIdx * 3 + 2];

        const float dx = wx - GetPosX();
        const float dz = wz - GetPosZ();
        const float dist = std::sqrt(dx * dx + dz * dz);

        // 현재 waypoint 가 경로의 마지막(= 최종 목적지)인지 확인한다.
        const bool isLastWaypoint = (m_curWaypointIdx * 3 + 5 >= static_cast<int32>(m_waypoints.size()));

        if (dist <= remainMoveDist)
        {
            if (isLastWaypoint)
            {
                // 최종 목적지에 이번 tick 안에 닿을 수 있다.
                // 슬롯에 칼같이 스냅(SetPos)하면 마지막 자투리만큼 순간이동처럼 보인다.
                // 대신 이번 tick 에 갈 수 있는 만큼만 직선 이동하고, 나머지는 다음 tick 에 마저 간다.
                // (도착이 최대 1 tick 늦어질 뿐, 점프가 사라진다. 슬롯은 hint 라 정밀 도달 불필요.)
                const float nx = dx / dist;
                const float nz = dz / dist;
                const float ratio = remainMoveDist / dist;
                const float dy = wy - GetPosY();
                SetPos(GetPosX() + nx * remainMoveDist,
                    GetPosY() + dy * ratio,
                    GetPosZ() + nz * remainMoveDist);
                remainMoveDist = 0.0f;
                continue;
            }

            // 중간 waypoint: 스냅 후 자투리 거리로 다음 구간을 이어서 이동(연속적, 점프 없음).
            SetPos(wx, wy, wz);
            remainMoveDist -= dist;
            ++m_curWaypointIdx;
            faceWaypoint();
            continue;
        }

        // 이번 tick 에는 도달 못 함. 방향으로 remainMoveDist 만큼 이동. Y 는 비례 보간.
        const float nx = dx / dist;
        const float nz = dz / dist;
        const float ratio = remainMoveDist / dist;
        const float dy = wy - GetPosY();
        SetPos(GetPosX() + nx * remainMoveDist,
               GetPosY() + dy * ratio,
               GetPosZ() + nz * remainMoveDist);
        remainMoveDist = 0.0f;
    }

    return false;
}

void Monster::faceWaypoint()
{
    const float wx = m_waypoints[m_curWaypointIdx * 3 + 0];
    const float wz = m_waypoints[m_curWaypointIdx * 3 + 2];
    const float dx = wx - GetPosX();
    const float dz = wz - GetPosZ();
    if (dx * dx + dz * dz < k_minMoveDistSq)
        return;
    // Unity 호환: yaw_deg = atan2(dx, dz) * 180/PI (+Z 정면).
    SetYaw(std::atan2(dx, dz) * k_radToDeg);
}
