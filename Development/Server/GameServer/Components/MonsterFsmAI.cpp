#include "pch.h"
#include "Components/MonsterFsmAI.h"
#include "StageObjects/Monster.h"
#include "Stages/Stage.h"

#include <cmath>

namespace
{
    constexpr float k_rangedBandRatio = 0.8f;    // 원거리 유지거리 허용 하한 비율 (떨림 방지)
    constexpr float k_attackLeaveMargin = 0.5f;  // Attack→Chase 이탈 히스테리시스 여유(유닛). 경계 토글 방지.
    constexpr float k_returnArriveDistSq = 0.25f;   // 스폰지점 0.5유닛 이내면 복귀 완료

    // 배회(Wander).
    constexpr float k_wanderArriveDistSq = 1.0f;    // 배회 목적지 1유닛 이내면 도착
    constexpr int64 k_wanderTimeoutMs    = 6000;    // 배회 이동 최대시간(막히면 포기하고 Idle 복귀)

    // 전투 중 위치 변경.
    constexpr float k_repositionArriveDistSq = 1.0f;
    constexpr int64 k_repositionTimeoutMs = 6000;
    constexpr int32 k_repositionSampleAttempts = 8;
    constexpr float k_repositionLeashMargin = 1.0f;

    // 근접 추격 시 타겟 한 점으로 몰리는(겹침) 것을 막기 위한 어택 슬롯.
    // 타겟 둘레에 슬롯을 2중 링(안/바깥)으로 배치하고, 몬스터마다 objectId % (총슬롯수) 로
    // 슬롯을 정해 그 좌표를 이동 목적지로 삼는다. 슬롯은 서버 내부 dest 분산용 hint 일 뿐이며
    // (클라는 평소처럼 dest 1개를 받을 뿐 슬롯 개념을 모름), 정확히 도달할 필요는 없다.
    //   - 안쪽 링: 공격사거리 안쪽 반지름 → 도착 시 inAttackBand 판정을 통과해 바로 공격.
    //   - 바깥 링: 공격사거리 밖 반지름 → 대기/접근용 자리. 안쪽이 비면 inAttackBand 로 자연히 진입.
    // 추가로 같은 슬롯에 배정된 몬스터들이 정확히 겹치지 않도록 objectId 기반 지터를 더한다.
    // 지터는 objectId 로만 결정되어 매 tick 동일 → dest 가 떨리지 않아 repath throttle 과 안전.
    constexpr int32 k_attackSlotInnerCount = 8;     // 안쪽 링 슬롯 개수
    constexpr int32 k_attackSlotOuterCount = 12;    // 바깥 링 슬롯 개수
    constexpr int32 k_attackSlotTotalCount = k_attackSlotInnerCount + k_attackSlotOuterCount;
    // 두 링 모두 공격사거리 안에 들어오게 1.0 미만으로 둔다(바깥 링 몬스터도 공격 가능).
    // attackRange=2.0 기준: 안쪽≈1.1u, 바깥≈1.7u (+지터 ±0.25 → 최대 1.95 < 2.0).
    constexpr float k_attackSlotInnerRatio = 0.55f;  // 안쪽 링 반지름 = 공격사거리 * 이 비율
    constexpr float k_attackSlotOuterRatio = 0.85f;  // 바깥 링 반지름 = 공격사거리 * 이 비율
    constexpr float k_attackSlotAngleJitter = 0.15f; // 각도 지터 최대치 (rad)
    constexpr float k_attackSlotRadiusJitter = 0.25f; // 반지름 지터 최대치 (유닛)
    constexpr float k_twoPi = 6.2831853f;

    // 몬스터→타겟 시야 판정. 벽/절벽 너머면 false → 공격 불가(계속 접근/재배치).
    // Stage 가 없으면 true(가시). NavMesh raycast 라 지표 2D 판정(입체지형 부정확).
    bool hasLineOfSightToTarget(const Monster& monster, const StageObject& target)
    {
        const Stage* pStage = monster.GetStage();
        if (pStage == nullptr)
            return true;
        return pStage->HasLineOfSight(monster.GetPosX(), monster.GetPosY(), monster.GetPosZ(),
                                      target.GetPosX(),  target.GetPosY(),  target.GetPosZ());
    }
}

void MonsterFsmAI::Update(Monster& monster, int64 deltaMs)
{
    // 사망 우선 처리.
    if (m_state == EMonsterState::Dead)
        return;

    if (monster.IsDead())
    {
        enterDead(monster);
        return;
    }

    // 전투 위치 변경 타이머는 시전/후딜 중에도 경과하되, 이동은 잠금이 끝난 후에만 시작한다.
    if ((m_state == EMonsterState::Chase || m_state == EMonsterState::Attack) &&
        monster.GetCombatRepositionIntervalMs() > 0)
    {
        if (m_combatRepositionRemainingMs < 0)
            armCombatReposition(monster);
        else if (m_combatRepositionRemainingMs > 0)
            m_combatRepositionRemainingMs -= deltaMs;
    }

    // 캐스트(윈드업/회복) 중에는 새 의사결정을 하지 않는다. 진행은 Monster::advanceCast 가 담당(커밋 유지).
    if (monster.GetCombat().IsCastBusy())
        return;

    switch (m_state)
    {
    case EMonsterState::Idle:    updateIdle(monster, deltaMs);    break;
    case EMonsterState::Wander:  updateWander(monster, deltaMs);  break;
    case EMonsterState::Chase:   updateChase(monster, deltaMs);   break;
    case EMonsterState::Attack:  updateAttack(monster, deltaMs);  break;
    case EMonsterState::Reposition: updateReposition(monster, deltaMs); break;
    case EMonsterState::Return:  updateReturn(monster, deltaMs);  break;
    case EMonsterState::Dead:    break;
    }
}

void MonsterFsmAI::OnProvoked(Monster& monster)
{
    if (m_state == EMonsterState::Dead)
        return;

    // 비교전(Idle/Wander/Return)이면 즉시 추격 전환 + 업데이트 주기 승격.
    // ※ Wander 를 포함해야 한다: 배회 중 어그로 범위 밖에서 맞은 경우, 여기서 Chase 로 안 넘기면
    //   다음 updateWander 의 AcquireTarget 이 (범위 밖이라) 강제 타겟을 지워버려 반격이 무효화된다.
    if (m_state == EMonsterState::Idle || m_state == EMonsterState::Wander || m_state == EMonsterState::Return)
    {
        monster.StopMoving();       // 배회/복귀 이동 중이었으면 멈추고 추격 시작.
        monster.SetEngagedTick();
        armCombatReposition(monster);
        m_state = EMonsterState::Chase;
    }
}

void MonsterFsmAI::updateIdle(Monster& monster, int64 deltaMs)
{
    // 주변 어그로 타겟 탐색. 찾으면 추격으로 전환.
    monster.GetCombat().AcquireTarget();
    if (monster.GetCombat().HasTarget())
    {
        monster.SetEngagedTick();   // 관여 시작 → 업데이트 주기 승격(engaged, 예 100ms).
        armCombatReposition(monster);
        m_state = EMonsterState::Chase;
        return;
    }

    // ── 배회 ── (반경 0 이면 비활성)
    const float wanderRadius = monster.GetWanderRadius();
    if (wanderRadius <= 0.0f)
        return;

    // 첫 진입 시 다음 배회 시각을 랜덤 예약(스폰 직후 일제히 배회하지 않도록).
    if (m_nextWanderMs < 0)
    {
        scheduleNextWander(monster);
        return;
    }

    m_nextWanderMs -= deltaMs;
    if (m_nextWanderMs > 0)
        return;

    // 배회 시작: spawn 중심 반경 내 랜덤점(원판 균일 분포 → sqrt).
    const float angle  = rand01(monster) * k_twoPi;
    const float radius = wanderRadius * std::sqrt(rand01(monster));
    m_wanderDestX = monster.GetSpawnX() + std::cos(angle) * radius;
    m_wanderDestZ = monster.GetSpawnZ() + std::sin(angle) * radius;
    m_wanderElapsedMs = 0;
    // 배회 이동을 부드럽게 하려고 업데이트 주기를 승격한다(idle 500ms → engaged, 예 100ms).
    // 500ms 로는 위치가 500ms 간격으로만 갱신돼 뚝뚝 끊겨 보인다(추격이 부드러운 이유 = engaged tick).
    // 서있는 Idle 로 돌아가면 updateWander 가 다시 SetIdleTick 으로 강등한다.
    monster.SetEngagedTick();
    m_state = EMonsterState::Wander;
}

void MonsterFsmAI::updateWander(Monster& monster, int64 deltaMs)
{
    // 배회 중에도 어그로 탐색 — 적 발견 시 즉시 정지 후 교전.
    monster.GetCombat().AcquireTarget();
    if (monster.GetCombat().HasTarget())
    {
        monster.StopMoving();
        monster.SetEngagedTick();
        m_state = EMonsterState::Chase;
        return;
    }

    m_wanderElapsedMs += deltaMs;

    const float dx = m_wanderDestX - monster.GetPosX();
    const float dz = m_wanderDestZ - monster.GetPosZ();
    const bool arrived = (dx * dx + dz * dz) <= k_wanderArriveDistSq;

    // 도착 또는 (막혀서) 타임아웃 → 정지 + 다음 배회 예약 + Idle 복귀.
    if (arrived || m_wanderElapsedMs >= k_wanderTimeoutMs)
    {
        monster.StopMoving();
        monster.SetIdleTick();   // 배회 끝(서있음) → 업데이트 주기를 다시 idle(500ms)로 강등.
        scheduleNextWander(monster);
        m_state = EMonsterState::Idle;
        return;
    }

    monster.MoveTo(m_wanderDestX, monster.GetSpawnY(), m_wanderDestZ, deltaMs);
}

// 다음 배회까지 남은시간을 [min,max] 랜덤으로 예약.
void MonsterFsmAI::scheduleNextWander(Monster& monster)
{
    int64 lo = monster.GetWanderMinIntervalMs();
    int64 hi = monster.GetWanderMaxIntervalMs();
    if (hi < lo) hi = lo;
    m_nextWanderMs = lo + static_cast<int64>(rand01(monster) * static_cast<float>(hi - lo));
}

// 몬스터별 경량 RNG(xorshift32). 첫 사용 시 objectId 로 시드(0 회피).
uint32 MonsterFsmAI::nextRand(Monster& monster)
{
    if (m_rngState == 0)
        m_rngState = static_cast<uint32>(monster.GetObjectId() * 2654435761ULL) | 1u;
    m_rngState ^= m_rngState << 13;
    m_rngState ^= m_rngState >> 17;
    m_rngState ^= m_rngState << 5;
    return m_rngState;
}

float MonsterFsmAI::rand01(Monster& monster)
{
    return static_cast<float>(nextRand(monster) & 0xFFFFFFu) / static_cast<float>(0x1000000);
}

void MonsterFsmAI::armCombatReposition(Monster& monster)
{
    m_combatRepositionRemainingMs = monster.GetCombatRepositionIntervalMs();
}

bool MonsterFsmAI::tryBeginCombatReposition(Monster& monster)
{
    Stage* pStage = monster.GetStage();
    const float minDistance = monster.GetCombatRepositionMinDistance();
    const float maxDistance = monster.GetCombatRepositionMaxDistance();
    if (pStage == nullptr || maxDistance <= 0.0f || maxDistance < minDistance)
    {
        armCombatReposition(monster);
        return false;
    }

    const float leashLimit = monster.GetLeashRange() - k_repositionLeashMargin;
    if (leashLimit <= 0.0f)
    {
        armCombatReposition(monster);
        return false;
    }

    const float minDistanceSq = minDistance * minDistance;
    const float leashLimitSq = leashLimit * leashLimit;
    for (int32 attempt = 0; attempt < k_repositionSampleAttempts; ++attempt)
    {
        float destX = 0.0f, destY = 0.0f, destZ = 0.0f;
        if (!pStage->SampleRandomNavPoint(monster.GetPosX(), monster.GetPosY(), monster.GetPosZ(),
                                         maxDistance, destX, destY, destZ))
            continue;

        const float currentDx = destX - monster.GetPosX();
        const float currentDz = destZ - monster.GetPosZ();
        if (currentDx * currentDx + currentDz * currentDz < minDistanceSq)
            continue;

        const float spawnDx = destX - monster.GetSpawnX();
        const float spawnDz = destZ - monster.GetSpawnZ();
        if (spawnDx * spawnDx + spawnDz * spawnDz > leashLimitSq)
            continue;

        m_repositionDestX = destX;
        m_repositionDestY = destY;
        m_repositionDestZ = destZ;
        m_repositionElapsedMs = 0;
        monster.StopMoving();
        m_state = EMonsterState::Reposition;
        armCombatReposition(monster);
        return true;
    }

    armCombatReposition(monster);
    return false;
}

void MonsterFsmAI::updateChase(Monster& monster, int64 deltaMs)
{
    StageObject* pTarget = monster.GetCombat().GetTarget();
    if (pTarget == nullptr)
    {
        monster.StopMoving();
        m_state = EMonsterState::Return;
        return;
    }

    // 리쉬: 스폰지점에서 너무 멀어지면 추격 포기 후 복귀.
    const float leash = monster.GetLeashRange();
    if (monster.DistSqFromSpawn() > leash * leash)
    {
        monster.StopMoving();
        m_state = EMonsterState::Return;
        return;
    }

    const float dx = pTarget->GetPosX() - monster.GetPosX();
    const float dz = pTarget->GetPosZ() - monster.GetPosZ();
    const float dist = std::sqrt(dx * dx + dz * dz);

    // 공격 가능 위치 판정 (근접/원거리 구분).
    bool inAttackBand = monster.IsRanged()
        ? (dist <= monster.GetCombat().GetMaxAttackRange() && dist >= monster.GetDesiredRange() * k_rangedBandRatio)
        : (dist <= monster.GetCombat().GetMaxAttackRange());
    // 사거리 안이어도 벽 너머면 공격 불가 → 계속 접근/재배치(길찾기가 벽을 돌아가게 함).
    // (거리 조건을 통과한 경우에만 raycast 하므로 원거리 추격 내내 쏘지 않는다.)
    if (inAttackBand && !hasLineOfSightToTarget(monster, *pTarget))
        inAttackBand = false;
    if (inAttackBand)
    {
        monster.StopMoving();
        monster.FaceTarget(pTarget);
        m_state = EMonsterState::Attack;
        return;
    }

    // 이동 목표 계산.
    // 근접: 타겟 정중앙이 아니라 타겟 둘레의 어택 슬롯으로 향한다 (겹침 방지).
    //       slot = objectId % 총슬롯수. 안쪽 링부터 채우고, 넘치면 바깥 링.
    //       같은 슬롯끼리도 objectId 지터로 흩어 정확한 포개짐을 방지한다.
    const int64 objectId = monster.GetObjectId();
    const int32 slot = static_cast<int32>(objectId % k_attackSlotTotalCount);

    int32 ringSlotIndex;   // 링 내에서의 슬롯 순번
    int32 ringSlotCount;   // 그 링의 총 슬롯 수
    float ringRatio;       // 그 링의 반지름 비율
    if (slot < k_attackSlotInnerCount)
    {
        ringSlotIndex = slot;
        ringSlotCount = k_attackSlotInnerCount;
        ringRatio = k_attackSlotInnerRatio;
    }
    else
    {
        ringSlotIndex = slot - k_attackSlotInnerCount;
        ringSlotCount = k_attackSlotOuterCount;
        ringRatio = k_attackSlotOuterRatio;
    }

    // objectId 기반 지터 (deterministic). 서로 다른 해시로 각도/반지름에 분산.
    const float angleJitter = ((objectId * 2654435761LL) % 1000) / 1000.0f;   // 0.0 ~ 1.0
    const float radiusJitter = ((objectId * 40503LL) % 1000) / 1000.0f;        // 0.0 ~ 1.0

    const float slotAngle = (k_twoPi / ringSlotCount) * ringSlotIndex
        + (angleJitter - 0.5f) * 2.0f * k_attackSlotAngleJitter;
    const float slotRadius = monster.GetCombat().GetMaxAttackRange() * ringRatio
        + (radiusJitter - 0.5f) * 2.0f * k_attackSlotRadiusJitter;

    float destX = pTarget->GetPosX() + std::cos(slotAngle) * slotRadius;
    float destZ = pTarget->GetPosZ() + std::sin(slotAngle) * slotRadius;
    if (monster.IsRanged())
    {
        // 타겟으로부터 desiredRange 만큼 떨어진 "몬스터 쪽 방향"의 점 (카이팅).
        float dirX = monster.GetPosX() - pTarget->GetPosX();
        float dirZ = monster.GetPosZ() - pTarget->GetPosZ();
        const float len = std::sqrt(dirX * dirX + dirZ * dirZ);
        if (len > 0.0001f)
        {
            dirX /= len;
            dirZ /= len;
        }
        else
        {
            dirX = 0.0f;
            dirZ = 1.0f;
        }
        destX = pTarget->GetPosX() + dirX * monster.GetDesiredRange();
        destZ = pTarget->GetPosZ() + dirZ * monster.GetDesiredRange();
    }

    monster.MoveTo(destX, pTarget->GetPosY(), destZ, deltaMs);
}

void MonsterFsmAI::updateAttack(Monster& monster, int64 /*deltaMs*/)
{
    StageObject* pTarget = monster.GetCombat().GetTarget();
    if (pTarget == nullptr)
    {
        m_state = EMonsterState::Return;
        return;
    }

    if (monster.GetCombatRepositionIntervalMs() > 0 &&
        m_combatRepositionRemainingMs <= 0 &&
        tryBeginCombatReposition(monster))
        return;

    const float dx = pTarget->GetPosX() - monster.GetPosX();
    const float dz = pTarget->GetPosZ() - monster.GetPosZ();
    const float dist = std::sqrt(dx * dx + dz * dz);

    // 사거리를 벗어났으면 다시 추격 (원거리는 너무 가까워도 재배치).
    // 히스테리시스: Chase→Attack 진입은 attackRange 이내(updateChase)지만, Attack→Chase 이탈은
    // attackRange + 여유(k_attackLeaveMargin) 초과일 때만. 경계에서 두 상태를 오가며 생기는
    // 미세 떨림(재배치 반복)을 막는다.
    bool needReposition = (dist > monster.GetCombat().GetMaxAttackRange() + k_attackLeaveMargin);
    if (monster.IsRanged() && dist < monster.GetDesiredRange() * k_rangedBandRatio)
        needReposition = true;

    // 타겟이 벽 뒤로 들어가 시야가 막히면 재배치(추격)로 전환 → 벽 너머로 공격하지 않는다.
    if (!needReposition && !hasLineOfSightToTarget(monster, *pTarget))
        needReposition = true;

    if (needReposition)
    {
        m_state = EMonsterState::Chase;
        return;
    }

    monster.FaceTarget(pTarget);

    // 사용 가능한 스킬 선택 → 시전 시작. 윈드업/발동/회복/통보는 Monster(몸체)가 단일 지점에서 처리한다.
    // 시전이 시작되면 다음 tick 부터 monster.IsCastBusy() 가 true 라 Update 가 일찍 반환(커밋 유지).
    int32 idx = monster.GetCombat().SelectReadySkill(dist);
    if (idx >= 0)
    {
        monster.GetCombat().RetargetForSkill();
        pTarget = monster.GetCombat().GetTarget();
        if (pTarget == nullptr)
            return;

        const float targetDx = pTarget->GetPosX() - monster.GetPosX();
        const float targetDz = pTarget->GetPosZ() - monster.GetPosZ();
        const float targetDist = std::sqrt(targetDx * targetDx + targetDz * targetDz);
        idx = monster.GetCombat().SelectReadySkill(targetDist);
        if (idx >= 0)
            (void)monster.GetCombat().TryBeginCast(idx, pTarget);
    }
    // 쓸 스킬이 없으면 Attack 유지 (쿨다운 회복 대기).
}

void MonsterFsmAI::updateReposition(Monster& monster, int64 deltaMs)
{
    StageObject* pTarget = monster.GetCombat().GetTarget();
    if (pTarget == nullptr)
    {
        monster.StopMoving();
        m_state = EMonsterState::Return;
        return;
    }

    const float leashRange = monster.GetLeashRange();
    if (monster.DistSqFromSpawn() > leashRange * leashRange)
    {
        monster.StopMoving();
        m_state = EMonsterState::Return;
        return;
    }

    m_repositionElapsedMs += deltaMs;

    const float dx = m_repositionDestX - monster.GetPosX();
    const float dz = m_repositionDestZ - monster.GetPosZ();
    if (dx * dx + dz * dz <= k_repositionArriveDistSq || m_repositionElapsedMs >= k_repositionTimeoutMs)
    {
        monster.StopMoving();

        const float targetDx = pTarget->GetPosX() - monster.GetPosX();
        const float targetDz = pTarget->GetPosZ() - monster.GetPosZ();
        const float attackRange = monster.GetCombat().GetMaxAttackRange();
        m_state = (targetDx * targetDx + targetDz * targetDz <= attackRange * attackRange)
            ? EMonsterState::Attack
            : EMonsterState::Chase;
        return;
    }

    monster.MoveTo(m_repositionDestX, m_repositionDestY, m_repositionDestZ, deltaMs);
}

void MonsterFsmAI::updateReturn(Monster& monster, int64 deltaMs)
{
    if (monster.DistSqFromSpawn() <= k_returnArriveDistSq)
    {
        // 복귀 완료 → 풀피 회복 후 대기.
        monster.StopMoving();
        monster.SnapToSpawn();
        monster.FillHp();
        monster.FillMp();
        monster.GetCombat().ClearTarget();
        monster.SetIdleTick();   // 비관여 복귀 → 업데이트 주기 강등(idle, 500ms).
        scheduleNextWander(monster);   // 복귀 직후 곧바로 배회하지 않도록 타이머 재예약.
        m_combatRepositionRemainingMs = -1;
        m_state = EMonsterState::Idle;
        return;
    }

    // 복귀 중에는 어그로 무시 (흔한 패턴).
    monster.MoveTo(monster.GetSpawnX(), monster.GetSpawnY(), monster.GetSpawnZ(), deltaMs);
}

void MonsterFsmAI::enterDead(Monster& monster)
{
    monster.StopMoving();
    monster.GetCombat().ClearTarget();
    monster.GetCombat().CancelCast();   // 시전 중이었다면 취소(환불 없음).
    m_state = EMonsterState::Dead;
    // TODO(전투/스폰): 사망 처리(드롭/경험치/디스폰/리스폰)는 Stage 가 updateMonsters 루프 밖에서 수행.
}
