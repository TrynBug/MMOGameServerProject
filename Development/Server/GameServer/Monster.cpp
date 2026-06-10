#include "pch.h"
#include "Monster.h"
#include "Stage.h"
#include "Sector.h"

#include <cmath>

namespace
{
    // ── 이동 임계값 (FaceTarget yaw 계산용) ──
    constexpr float k_minMoveDistSq = 0.01f;     // 0.1유닛 이내면 회전 안 함
    constexpr float k_radToDeg      = 57.2957795f;

    // throttled repath: 목표점이 1유닛 이상 움직이면 재길찾기.
    constexpr float k_repathDistSq = 1.0f;

    // 사망 후 시체 유지 시간(ms). 이 시간이 지나면 디스폰한다.
    constexpr int64 k_corpseDurationMs = 30000;
}

bool Monster::Initialize(int64 objectId, const GameData_Monster* pMonsterData)
{
    if (pMonsterData == nullptr)
    {
        LOG_WRITE(LogLevel::Error, std::format("monster data is null. objectId={}", objectId));
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
    // v1 대미지: 종류 데이터의 Str(물리) 총합을 기준으로. 데이터에 Str 이 없으면 fallback.
    const double baseAtk = GetStatTotal(EStatGroup::Str);
    const double atk = (baseAtk > 0.0) ? baseAtk : 5.0;

    MonsterSkill basicAttack;
    basicAttack.skillId    = 1;
    basicAttack.range      = m_attackRange;
    basicAttack.cooldownMs = 1500;
    basicAttack.castTimeMs = 0;
    basicAttack.damage     = atk;
    m_skills.push_back(basicAttack);

    MonsterSkill castSkill;
    castSkill.skillId    = 2;
    castSkill.range      = m_attackRange;
    castSkill.cooldownMs = 5000;
    castSkill.castTimeMs = 800;
    castSkill.damage     = atk * 2.0;   // 시전 스킬은 더 큰 대미지.
    m_skills.push_back(castSkill);

    return true;
}

bool Monster::applyBaseStats()
{
    if (m_pMonsterData == nullptr)
    {
        LOG_WRITE(LogLevel::Error, std::format("monster data is null. objectId={}", GetObjectId()));
        return false;
    }

    // (EStat, value) 쌍 목록을 순회하며 적용. Stat 이 None 인 슬롯은 건너뛴다.
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
    if (pTarget == nullptr)
        return;

    Stage* pStage = GetStage();
    if (pStage == nullptr)
        return;

    // 대상은 유저(캐릭터)여야 한다 (진영 규칙: 몬스터 → 유저). 사망한 대상은 제외.
    if (pTarget->GetObjectType() != EObjectType::User)
        return;
    ActorObject* pTargetActor = static_cast<ActorObject*>(pTarget);
    if (pTargetActor->IsDead())
        return;

    const MonsterSkill& skill = m_skills[index];

    // 서버 권위 대미지 적용 + 주변 AOI 에 SkillDamageNtf(숫자/HP), 사망 시 ObjectDeathNtf 브로드캐스트.
    // (v1: 단일 대상 즉시 판정. 방어력 감산/원거리 투사체/공격 모션 통보는 후속.)
    pStage->ApplyEffectDamage(*pTargetActor, skill.damage, GetObjectId());
}

// ─────────────────────────────────────────────────────────────
// 공유 행동 레이어 — 이동
// ─────────────────────────────────────────────────────────────
void Monster::MoveTo(float destX, float destY, float destZ, int64 deltaMs)
{
    // throttled repath: 목표점이 충분히 바뀌었거나 정지 상태면 새 경로 계산.
    const float pdx = destX - m_pathTargetX;
    const float pdz = destZ - m_pathTargetZ;
    if (!m_mover.IsMoving() || !m_hasPathTarget || (pdx * pdx + pdz * pdz) > k_repathDistSq)
    {
        // Monster 는 매 tick repath 가능하므로 직선 폴백 로그는 끈다(스팸 방지).
        m_mover.SetDestination(*this, destX, destY, destZ, /*logFallback*/ false);
        m_pathTargetX  = destX;
        m_pathTargetZ  = destZ;
        m_hasPathTarget = true;
    }

    m_mover.Update(*this, deltaMs, m_moveSpeed);

    if (Stage* pStage = GetStage())
        pStage->UpdateObjectSector(this);

    // 이동 복제는 Stage::buildAndSendSnapshots 의 스냅샷 스트리밍이 담당한다(서버 권위 위치를 매 tick 송신).
}

void Monster::SnapToSpawn()
{
    SetPos(m_spawnX, m_spawnY, m_spawnZ);
    if (Stage* pStage = GetStage())
        pStage->UpdateObjectSector(this);
}

void Monster::StopMoving()
{
    m_mover.Stop();
    m_hasPathTarget = false;
}
