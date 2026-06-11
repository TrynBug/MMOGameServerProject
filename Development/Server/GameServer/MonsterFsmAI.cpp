#include "pch.h"
#include "MonsterFsmAI.h"
#include "Monster.h"

#include <cmath>

namespace
{
    constexpr float k_rangedBandRatio = 0.8f;    // 원거리 유지거리 허용 하한 비율 (떨림 방지)
    constexpr float k_attackLeaveMargin = 0.5f;  // Attack→Chase 이탈 히스테리시스 여유(유닛). 경계 토글 방지.
    constexpr float k_returnArriveDistSq = 0.25f;   // 스폰지점 0.5유닛 이내면 복귀 완료

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

    // 캐스트(윈드업/회복) 중에는 새 의사결정을 하지 않는다. 진행은 Monster::advanceCast 가 담당(커밋 유지).
    if (monster.GetCombat().IsCastBusy())
        return;

    switch (m_state)
    {
    case EMonsterState::Idle:    updateIdle(monster, deltaMs);    break;
    case EMonsterState::Chase:   updateChase(monster, deltaMs);   break;
    case EMonsterState::Attack:  updateAttack(monster, deltaMs);  break;
    case EMonsterState::Return:  updateReturn(monster, deltaMs);  break;
    case EMonsterState::Dead:    break;
    }
}

void MonsterFsmAI::updateIdle(Monster& monster, int64 /*deltaMs*/)
{
    // 주변 어그로 타겟 탐색. 찾으면 추격으로 전환.
    monster.GetCombat().AcquireTarget();
    if (monster.GetCombat().HasTarget())
    {
        monster.SetEngagedTick();   // 관여 시작 → 업데이트 주기 승격(engaged, 예 100ms).
        m_state = EMonsterState::Chase;
    }
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
    const bool inAttackBand = monster.IsRanged()
        ? (dist <= monster.GetCombat().GetMaxAttackRange() && dist >= monster.GetDesiredRange() * k_rangedBandRatio)
        : (dist <= monster.GetCombat().GetMaxAttackRange());
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

    if (needReposition)
    {
        m_state = EMonsterState::Chase;
        return;
    }

    monster.FaceTarget(pTarget);

    // 사용 가능한 스킬 선택 → 시전 시작. 윈드업/발동/회복/통보는 Monster(몸체)가 단일 지점에서 처리한다.
    // 시전이 시작되면 다음 tick 부터 monster.IsCastBusy() 가 true 라 Update 가 일찍 반환(커밋 유지).
    const int32 idx = monster.GetCombat().SelectReadySkill(dist);
    if (idx >= 0)
        (void)monster.GetCombat().TryBeginCast(idx, pTarget);
    // 쓸 스킬이 없으면 Attack 유지 (쿨다운 회복 대기).
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
