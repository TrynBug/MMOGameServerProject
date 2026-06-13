#include "pch.h"
#include "MonsterCombatComponent.h"

#include "Monster.h"                      // owner 완전타입 (위치/objectId/Stage/회전/이동 접근)
#include "Stage.h"
#include "Sector.h"                       // AcquireTarget 의 sector 순회
#include "Generated/GameData_Skill.h"
#include "Skill/SkillBake.h"             // GameData_Skill → EffectParams

#include <cmath>

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

// ─────────────────────────────────────────────────────────────
// 스킬 선택
// ─────────────────────────────────────────────────────────────
int32 MonsterCombatComponent::SelectReadySkill(float distToTarget) const
{
    // 목록 순서 = 우선순위. 쿨다운 끝 + 사거리 내 첫 스킬을 고른다.
    for (size_t i = 0; i < m_skills.size(); ++i)
    {
        const MonsterSkill& skill = m_skills[i];
        if (skill.remainingCooldownMs <= 0 && distToTarget <= skill.range)
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
    const float dx = pTarget->GetPosX() - m_pOwner->GetPosX();
    const float dz = pTarget->GetPosZ() - m_pOwner->GetPosZ();
    if (dx * dx + dz * dz > skill.range * skill.range)
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
        switch (pSkill->Placement)
        {
        case ESkillPlacement::Forward:
        {
            // 캐스터 전방으로 offset. CasterFrontDistance>0 이면 그 값, 아니면 OBB 길이의 절반(빔이 앞으로 뻗게).
            const float offset = (pSkill->CasterFrontDistance > 0.0f)
                ? pSkill->CasterFrontDistance
                : static_cast<float>(pSkill->ObbLength) * 0.5f;
            originX += dirX * offset;
            originZ += dirZ * offset;
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
        EffectParams p = BakeSkillEffectParams(*pSkill, EObjectType::Monster, m_pOwner->GetObjectId(), origin, dir, /*seed*/ 0);
        p.damageAmount = skill.damage;   // 스탯 결합 대미지(Initialize 계산)로 덮어쓴다 (bake 는 DamageCoeff flat).
        pStage->SpawnSkillAreaEffect(p);
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
        pStage->SpawnMonsterProjectile(p);
        pStage->BroadcastSkillCastNtf(*m_pOwner, skill.skillId, /*effectId*/ 0, origin, dir, /*seed*/ 0, /*moveDistance*/ 0.0f);
        return;
    }

    // ── None: 커밋된 단일 대상에 직격 (형태/투사체 없는 단순 공격).
    if (pTarget == nullptr || pTarget->GetObjectType() != EObjectType::User)
        return;
    ActorObject* pTargetActor = static_cast<ActorObject*>(pTarget);
    if (pTargetActor->IsDead())
        return;

    pStage->ApplyEffectDamage(*pTargetActor, skill.damage, m_pOwner->GetObjectId(), /*isDuplicate*/ false, skill.skillId);
}
