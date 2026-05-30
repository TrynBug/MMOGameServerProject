#include "pch.h"
#include "MonsterFsmAI.h"
#include "Monster.h"      // 두뇌는 Monster 의 공유 행동 API 를 호출하므로 완전타입 필요

#include <cmath>

namespace
{
    constexpr float k_rangedBandRatio    = 0.8f;    // 원거리 유지거리 허용 하한 비율 (떨림 방지)
    constexpr float k_returnArriveDistSq = 0.25f;   // 스폰지점 0.5유닛 이내면 복귀 완료
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

    switch (m_state)
    {
    case EMonsterState::Idle:    updateIdle(monster, deltaMs);    break;
    case EMonsterState::Chase:   updateChase(monster, deltaMs);   break;
    case EMonsterState::Attack:  updateAttack(monster, deltaMs);  break;
    case EMonsterState::Casting: updateCasting(monster, deltaMs); break;
    case EMonsterState::Return:  updateReturn(monster, deltaMs);  break;
    case EMonsterState::Dead:    break;
    }
}

void MonsterFsmAI::updateIdle(Monster& monster, int64 /*deltaMs*/)
{
    // 주변 어그로 타겟 탐색. 찾으면 추격으로 전환.
    monster.AcquireTarget();
    if (monster.HasTarget())
        m_state = EMonsterState::Chase;
}

void MonsterFsmAI::updateChase(Monster& monster, int64 deltaMs)
{
    StageObject* pTarget = monster.GetTarget();
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
        ? (dist <= monster.GetAttackRange() && dist >= monster.GetDesiredRange() * k_rangedBandRatio)
        : (dist <= monster.GetAttackRange());
    if (inAttackBand)
    {
        monster.StopMoving();
        monster.FaceTarget(pTarget);
        m_state = EMonsterState::Attack;
        return;
    }

    // 이동 목표 계산.
    float destX = pTarget->GetPosX();
    float destZ = pTarget->GetPosZ();
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
    StageObject* pTarget = monster.GetTarget();
    if (pTarget == nullptr)
    {
        m_state = EMonsterState::Return;
        return;
    }

    const float dx = pTarget->GetPosX() - monster.GetPosX();
    const float dz = pTarget->GetPosZ() - monster.GetPosZ();
    const float dist = std::sqrt(dx * dx + dz * dz);

    // 사거리를 벗어났으면 다시 추격 (원거리는 너무 가까워도 재배치).
    bool needReposition = (dist > monster.GetAttackRange());
    if (monster.IsRanged() && dist < monster.GetDesiredRange() * k_rangedBandRatio)
        needReposition = true;
    if (needReposition)
    {
        m_state = EMonsterState::Chase;
        return;
    }

    monster.FaceTarget(pTarget);

    // 사용 가능한 스킬 선택 → 시전(Casting) 진입.
    const int32 idx = monster.SelectReadySkill(dist);
    if (idx >= 0)
    {
        m_castingSkillIndex = idx;
        m_castRemainingMs   = monster.GetSkill(idx).castTimeMs;
        m_state = EMonsterState::Casting;
        // TODO(통신): 스킬 시전 시작을 주변 유저에게 통보(스킬 Ntf)하여 클라가 모션 재생.
    }
    // 쓸 스킬이 없으면 Attack 유지 (쿨다운 회복 대기).
}

void MonsterFsmAI::updateCasting(Monster& monster, int64 deltaMs)
{
    // 선딜 진행. 이 동안 이동/다른 행동 없이 잠금.
    m_castRemainingMs -= deltaMs;
    if (m_castRemainingMs > 0)
        return;

    // 시전 완료 → 스킬 발동 + 쿨다운 시작.
    StageObject* pTarget = monster.GetTarget();
    if (m_castingSkillIndex >= 0)
    {
        monster.ExecuteSkill(m_castingSkillIndex, pTarget);
        monster.StartSkillCooldown(m_castingSkillIndex);
    }
    m_castingSkillIndex = -1;

    m_state = (pTarget == nullptr) ? EMonsterState::Return : EMonsterState::Attack;
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
        monster.ClearTarget();
        m_state = EMonsterState::Idle;
        return;
    }

    // 복귀 중에는 어그로 무시 (흔한 패턴).
    monster.MoveTo(monster.GetSpawnX(), monster.GetSpawnY(), monster.GetSpawnZ(), deltaMs);
}

void MonsterFsmAI::enterDead(Monster& monster)
{
    monster.StopMoving();
    monster.ClearTarget();
    m_castingSkillIndex = -1;
    m_state = EMonsterState::Dead;
    // TODO(전투/스폰): 사망 처리(드롭/경험치/디스폰/리스폰)는 Stage 가 updateMonsters 루프 밖에서 수행.
}
