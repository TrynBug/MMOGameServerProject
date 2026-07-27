#include "pch.h"
#include "Components/MonsterCombatComponent.h"

#include "GameServer.h"
#include "StageObjects/Monster.h"                      // owner 완전타입 (위치/objectId/Stage/회전/이동 접근)
#include "Stages/Stage.h"
#include "Stages/Sector.h"                       // AcquireTarget 의 sector 순회
#include "Generated/GameData_Skill.h"
#include "Skills/SkillBake.h"             // GameData_Skill → EffectParams

#include <cmath>

namespace
{
    std::vector<Vector3> ComputeFanDirs(const Vector3& dir, int32 count, float fanAngleDeg)
    {
        if (count <= 1)
            return { dir };

        std::vector<Vector3> dirs;
        dirs.reserve(count);

        const float totalRad = fanAngleDeg * 0.01745329f;
        const float step = totalRad / static_cast<float>(count - 1);
        const float startRad = -totalRad * 0.5f;

        for (int32 i = 0; i < count; ++i)
        {
            const float angle = startRad + step * static_cast<float>(i);
            const float cs = std::cos(angle);
            const float sn = std::sin(angle);
            dirs.emplace_back(dir.x * cs - dir.z * sn, dir.y, dir.x * sn + dir.z * cs);
        }
        return dirs;
    }
}

// ─────────────────────────────────────────────────────────────
// 타겟팅(perception)
// ─────────────────────────────────────────────────────────────
void MonsterCombatComponent::AcquireTarget()
{
    Stage* pStage = m_pOwner->GetStage();
    if (pStage == nullptr)
        return;

    const float aggroRange = m_pOwner->GetAggroRange();

    // 어그로 범위를 덮는 sector 범위 계산 (최소 1).
    const double sectorSize = pStage->GetSectorSize();
    int32 sectorRange = 1;
    if (sectorSize > 0.0)
    {
        sectorRange = static_cast<int32>(std::ceil(aggroRange / sectorSize));
        if (sectorRange < 1)
            sectorRange = 1;
    }

    const float myX = m_pOwner->GetPosX();
    const float myZ = m_pOwner->GetPosZ();

    int64 bestId = 0;
    float bestSq = aggroRange * aggroRange;   // 이 값 이하인 유저만 후보.

    pStage->ForEachAdjacentSector(m_pOwner->GetCurSectorX(), m_pOwner->GetCurSectorZ(), sectorRange,
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

StageObject* MonsterCombatComponent::GetTarget() const
{
    if (m_targetObjectId == 0)
        return nullptr;
    Stage* pStage = m_pOwner->GetStage();
    if (pStage == nullptr)
        return nullptr;
    return pStage->FindObject(m_targetObjectId);   // 사라졌으면 nullptr.
}

void MonsterCombatComponent::RetargetForSkill()
{
    const float clusterRadius = m_pOwner->GetTargetClusterRadius();
    const float attackRange = GetMaxAttackRange();
    if (clusterRadius <= 0.0f || attackRange <= 0.0f)
        return;

    Stage* pStage = m_pOwner->GetStage();
    if (pStage == nullptr)
        return;

    const double sectorSize = pStage->GetSectorSize();
    int32 sectorRange = 1;
    if (sectorSize > 0.0)
    {
        sectorRange = static_cast<int32>(std::ceil(attackRange / sectorSize));
        if (sectorRange < 1)
            sectorRange = 1;
    }

    const float myX = m_pOwner->GetPosX();
    const float myY = m_pOwner->GetPosY();
    const float myZ = m_pOwner->GetPosZ();
    const float attackRangeSq = attackRange * attackRange;

    std::vector<StageObject*> candidates;
    pStage->ForEachAdjacentSector(m_pOwner->GetCurSectorX(), m_pOwner->GetCurSectorZ(), sectorRange,
        [&](Sector* pSector)
        {
            for (const auto& pair : pSector->GetUsers())
            {
                StageObject* pUser = pair.second;
                if (pUser == nullptr || static_cast<ActorObject*>(pUser)->IsDead())
                    continue;

                const float dx = pUser->GetPosX() - myX;
                const float dz = pUser->GetPosZ() - myZ;
                if (dx * dx + dz * dz > attackRangeSq)
                    continue;

                if (!pStage->HasLineOfSight(myX, myY, myZ,
                                            pUser->GetPosX(), pUser->GetPosY(), pUser->GetPosZ()))
                    continue;

                candidates.push_back(pUser);
            }
        });

    if (candidates.empty())
        return;

    const float clusterRadiusSq = clusterRadius * clusterRadius;
    StageObject* pBest = nullptr;
    int32 bestClusterCount = -1;
    float bestDistSq = 0.0f;

    for (StageObject* pCandidate : candidates)
    {
        int32 clusterCount = 0;
        for (const StageObject* pOther : candidates)
        {
            const float dx = pOther->GetPosX() - pCandidate->GetPosX();
            const float dz = pOther->GetPosZ() - pCandidate->GetPosZ();
            if (dx * dx + dz * dz <= clusterRadiusSq)
                ++clusterCount;
        }

        const float dx = pCandidate->GetPosX() - myX;
        const float dz = pCandidate->GetPosZ() - myZ;
        const float distSq = dx * dx + dz * dz;
        const bool candidateIsCurrent = pCandidate->GetObjectId() == m_targetObjectId;
        const bool bestIsCurrent = pBest != nullptr && pBest->GetObjectId() == m_targetObjectId;

        if (pBest == nullptr || clusterCount > bestClusterCount ||
            (clusterCount == bestClusterCount && candidateIsCurrent && !bestIsCurrent) ||
            (clusterCount == bestClusterCount && candidateIsCurrent == bestIsCurrent && distSq < bestDistSq))
        {
            pBest = pCandidate;
            bestClusterCount = clusterCount;
            bestDistSq = distSq;
        }
    }

    if (pBest != nullptr)
        m_targetObjectId = pBest->GetObjectId();
}

// ─────────────────────────────────────────────────────────────
// 스킬 선택
// ─────────────────────────────────────────────────────────────
int32 MonsterCombatComponent::SelectReadySkill(float distToTarget) const
{
    // 목록 순서 = 우선순위. 쿨다운 끝 + 사거리 조건을 만족하는 첫 스킬을 고른다.
    // IgnoreSkillRange AI는 Attack 진입 거리(GetMaxAttackRange)는 유지하고 개별 스킬 거리만 무시한다.
    for (size_t i = 0; i < m_skills.size(); ++i)
    {
        const MonsterSkill& skill = m_skills[i];
        if (skill.remainingCooldownMs <= 0 && (m_pOwner->IgnoresSkillRange() || distToTarget <= skill.range))
            return static_cast<int32>(i);
    }
    return -1;
}

void MonsterCombatComponent::startCooldown(int32 index)
{
    if (index < 0 || index >= static_cast<int32>(m_skills.size()))
        return;
    m_skills[index].remainingCooldownMs = m_skills[index].cooldownMs;
}

uint32 MonsterCombatComponent::nextScatterSeed(int32 skillId)
{
    ++m_scatterSequence;
    const uint64 objectId = static_cast<uint64>(m_pOwner->GetObjectId());
    uint32 seed = static_cast<uint32>(objectId) ^ static_cast<uint32>(objectId >> 32);
    seed ^= static_cast<uint32>(skillId) * 0x9E3779B9u;
    seed ^= m_scatterSequence * 0x85EBCA6Bu;
    return seed != 0 ? seed : 0xA341316Cu;
}

void MonsterCombatComponent::tickCooldowns(int64 deltaMs)
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
// 매 tick: 쿨다운 + 캐스트 페이즈 진행 (integrate). 두뇌보다 먼저 호출된다.
// ─────────────────────────────────────────────────────────────
void MonsterCombatComponent::Update(int64 deltaMs)
{
    tickCooldowns(deltaMs);
    advanceCast(deltaMs);
}

// ─────────────────────────────────────────────────────────────
// 캐스트 시작 (두뇌의 단일 진입점)
// ─────────────────────────────────────────────────────────────
bool MonsterCombatComponent::TryBeginCast(int32 skillIndex, StageObject* pTarget)
{
    if (IsCastBusy())                                              // 이미 캐스트/회복 중
        return false;
    if (pTarget == nullptr)
        return false;
    if (skillIndex < 0 || skillIndex >= static_cast<int32>(m_skills.size()))
        return false;

    const MonsterSkill& skill = m_skills[skillIndex];
    if (skill.remainingCooldownMs > 0)                            // 쿨다운 미회복
        return false;

    // 사거리 재확인 (두뇌가 SelectReadySkill 로 골랐어도 한 번 더 가드).
    // IgnoreSkillRange AI는 이미 FSM의 최대 공격거리 안에서만 이 함수에 진입한다.
    const float dx = pTarget->GetPosX() - m_pOwner->GetPosX();
    const float dz = pTarget->GetPosZ() - m_pOwner->GetPosZ();
    if (!m_pOwner->IgnoresSkillRange() && dx * dx + dz * dz > skill.range * skill.range)
        return false;

    // 커밋: 타겟 고정 + 그쪽으로 1회 회전 + 이동 정지.
    m_pOwner->FaceTarget(pTarget);
    m_castTargetObjectId = pTarget->GetObjectId();
    m_pOwner->StopMoving();

    // 효과 방향 커밋. dir = 타겟 방향(정규화 X-Z).
    float dirX = dx, dirZ = dz;
    const float len = std::sqrt(dx * dx + dz * dz);
    if (len > 0.0001f) { dirX /= len; dirZ /= len; }
    else               { dirX = 0.0f; dirZ = 1.0f; }

    // 효과 기준점(origin) 을 스킬 Placement 로 산출 (텔레그래프·발동이 동일 기준 사용).
    float originX = m_pOwner->GetPosX(), originY = m_pOwner->GetPosY(), originZ = m_pOwner->GetPosZ();
    if (const GameData_Skill* pSkill = GameDataTable_Skill::FindData(skill.skillId))
    {
        // Placement는 효과 중심의 기준점만 정한다. 앵커와 전방 중심 보정은 모든 효과 타입에서 같은 규칙을 쓴다.
        switch (pSkill->Placement)
        {
        case ESkillPlacement::SkillCastOrigin:
        {
            const GameData_Monster* pMonsterData = m_pOwner->GetMonsterData();
            const Vector3 offset = pMonsterData != nullptr
                ? GameServer::Instance().GetCastAnchorRegistry().GetMonsterLocalOffset(pMonsterData->PrefabPath)
                : Vector3();
            originX += dirZ * offset.x + dirX * offset.z;
            originY += offset.y;
            originZ += -dirX * offset.x + dirZ * offset.z;
            break;
        }
        case ESkillPlacement::Target:
            originX = pTarget->GetPosX();
            originY = pTarget->GetPosY();
            originZ = pTarget->GetPosZ();
            break;
        default:
            break;   // Caster / None: 캐스터 위치 그대로
        }

        originX += dirX * pSkill->EffectCenterForwardOffset;
        originZ += dirZ * pSkill->EffectCenterForwardOffset;
    }

    // Windup 진입. 발동/쿨다운/회복은 advanceCast 가 처리한다.
    m_castSkillIndex   = skillIndex;
    m_castPhase        = EMonsterCastPhase::Windup;
    m_castRemainingMs  = skill.castTimeMs;
    m_castOriginX = originX; m_castOriginY = originY; m_castOriginZ = originZ;
    m_castDirX = dirX;       m_castDirZ = dirZ;

    // 클라에 시전 "시작" 통보(윈드업/텔레그래프). 커밋된 origin/dir 사용.
    if (Stage* pStage = m_pOwner->GetStage())
        pStage->BroadcastAbilityCastNtf(*m_pOwner, skill.skillId, m_castTargetObjectId,
            Vector3(originX, originY, originZ), Vector3(dirX, 0.0f, dirZ),
            static_cast<int32>(skill.castTimeMs));
    return true;
}

// ─────────────────────────────────────────────────────────────
// 캐스트 페이즈 진행 (Windup → Strike → Recovery → None)
// ─────────────────────────────────────────────────────────────
void MonsterCombatComponent::advanceCast(int64 deltaMs)
{
    if (m_castPhase == EMonsterCastPhase::None)
        return;

    m_castRemainingMs -= deltaMs;
    if (m_castRemainingMs > 0)
        return;

    if (m_castPhase == EMonsterCastPhase::Windup)
    {
        // 윈드업 만료 → 효과 발동 + 쿨다운 시작 → Recovery(후딜) 진입.
        onCastStrike();
        const int64 recoveryMs = (m_castSkillIndex >= 0 && m_castSkillIndex < static_cast<int32>(m_skills.size()))
            ? m_skills[m_castSkillIndex].actionLockMs : 0;
        m_castPhase       = EMonsterCastPhase::Recovery;
        m_castRemainingMs = recoveryMs;
    }
    else // Recovery
    {
        // 후딜 만료 → 캐스트 종료. 두뇌가 다음 행동을 고를 수 있다.
        m_castPhase      = EMonsterCastPhase::None;
        m_castSkillIndex = -1;
    }
}

void MonsterCombatComponent::onCastStrike()
{
    // 커밋된 타겟을 지금 해소(윈드업 중 despawn 가능 → nullptr 면 executeSkill 이 안전하게 무시).
    Stage* pStage = m_pOwner->GetStage();
    StageObject* pTarget = (pStage != nullptr) ? pStage->FindObject(m_castTargetObjectId) : nullptr;

    executeSkill(m_castSkillIndex, pTarget);
    startCooldown(m_castSkillIndex);
}

void MonsterCombatComponent::CancelCast()
{
    // 자원 환불 없음. 사망/CC 시 호출.
    m_castPhase       = EMonsterCastPhase::None;
    m_castSkillIndex  = -1;
    m_castRemainingMs = 0;
}

// ─────────────────────────────────────────────────────────────
// 효과 발동 (EffectDamage 별 dispatch). 커밋된 origin/dir 기준.
// ─────────────────────────────────────────────────────────────
void MonsterCombatComponent::executeSkill(int32 index, StageObject* pTarget)
{
    if (index < 0 || index >= static_cast<int32>(m_skills.size()))
        return;

    Stage* pStage = m_pOwner->GetStage();
    if (pStage == nullptr)
        return;

    const MonsterSkill& skill = m_skills[index];
    const GameData_Skill* pSkill = GameDataTable_Skill::FindData(skill.skillId);
    const ESkillEffectDamage effectDamage = (pSkill != nullptr) ? pSkill->EffectDamage : ESkillEffectDamage::None;

    // ── Area: 형태(Obb/Circle) 범위 판정 → 범위 내 모든 적에게 대미지. 텔레그래프와 동일 origin/shape.
    if (effectDamage == ESkillEffectDamage::Area)
    {
        const Vector3 origin(m_castOriginX, m_castOriginY, m_castOriginZ);
        const Vector3 dir(m_castDirX, 0.0f, m_castDirZ);
        const uint32 seed = (pSkill->ScatterCount > 1) ? nextScatterSeed(skill.skillId) : 0;
        EffectParams p = BakeSkillEffectParams(*pSkill, EObjectType::Monster, m_pOwner->GetObjectId(), origin, dir, seed);
        p.damageAmount = skill.damage;   // 스탯 결합 대미지(Initialize 계산)로 덮어쓴다 (bake 는 DamageCoeff flat).
        pStage->SpawnSkillAreaEffect(p);
        // 발동 통보 → 다른 클라(플레이어)/몬스터들이 EffectPrefabPath VFX 를 발동 시점에 재현한다.
        // (대미지는 서버 AreaEffect 가 SkillDamageNtf 로 구동. 이 Ntf 는 비주얼 전용.)
        pStage->BroadcastSkillCastNtf(*m_pOwner, skill.skillId, /*effectId*/ 0, origin, dir, seed, /*moveDistance*/ 0.0f);
        return;
    }

    // ── ContactHit: 서버 권위 투사체(매 tick 전진+충돌, 공정한 회피). 클라는 SkillCastNtf 로 비주얼만.
    if (effectDamage == ESkillEffectDamage::ContactHit)
    {
        const Vector3 origin(m_castOriginX, m_castOriginY, m_castOriginZ);
        const Vector3 dir(m_castDirX, 0.0f, m_castDirZ);
        EffectParams p = BakeSkillEffectParams(*pSkill, EObjectType::Monster, m_pOwner->GetObjectId(), origin, dir, /*seed*/ 0);
        p.damageAmount = skill.damage;
        p.motion       = ESkillEffectMotion::Linear;   // 투사체는 본질적으로 직선 이동(데이터 EffectMotion 무관).
        const std::vector<Vector3> dirs = ComputeFanDirs(
            dir, static_cast<int32>(pSkill->ProjectileCount), static_cast<float>(pSkill->FanAngleDeg));
        for (const Vector3& projectileDir : dirs)
        {
            EffectParams projectile = p;
            projectile.dir = projectileDir;
            projectile.shape.forward = projectileDir;
            pStage->SpawnMonsterProjectile(projectile);
        }
        pStage->BroadcastSkillCastNtf(*m_pOwner, skill.skillId, /*effectId*/ 0, origin, dir, /*seed*/ 0, /*moveDistance*/ 0.0f);
        return;
    }

    // ── None: 커밋된 단일 대상에 직격 (형태/투사체 없는 단순 공격).
    if (pTarget == nullptr || pTarget->GetObjectType() != EObjectType::Character)
        return;
    ActorObject* pTargetActor = static_cast<ActorObject*>(pTarget);
    if (pTargetActor->IsDead())
        return;

    pStage->ApplyEffectDamage(*pTargetActor, skill.damage, m_pOwner->GetObjectId(), /*isDuplicate*/ false, skill.skillId);
}
