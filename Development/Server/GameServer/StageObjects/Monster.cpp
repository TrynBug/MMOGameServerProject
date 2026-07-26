#include "pch.h"
#include "StageObjects/Monster.h"
#include "Stages/Stage.h"

#include "Generated/GameData_Skill.h"     // Initialize 의 스킬 스펙 로드(GameDataTable_Skill::FindData)
#include "Generated/GameData_MonsterAI.h" // AI 프로파일(aggro/leash/desired/engaged)

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

    // 이동속도 기본값(유닛/초). 종류 데이터에 MoveSpd 스탯이 없을 때의 fallback.
    // TODO(데이터): GameData_Monster 의 MoveSpd* 스탯으로 시드되면 이 fallback 은 불필요.
    constexpr double k_defaultMoveSpeed = 4.0;
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
    // applyBaseStats() 이후여야 HpTotal/MpTotal 이 계산되어 있어 최대치가 정해진다.
    FillHp();
    FillMp();

    // AI 프로파일 로드 (GameData_MonsterAI, AIKey 참조). 없으면 멤버 기본값 유지.
    if (const GameData_MonsterAI* pAI = GameDataTable_MonsterAI::FindData(m_pMonsterData->AIKey))
    {
        m_aggroRange              = pAI->AggroRange;
        m_leashRange              = pAI->LeashRange;
        m_desiredRange            = pAI->DesiredRange;
        m_ignoreSkillRange        = pAI->IgnoreSkillRange;
        m_engagedUpdateIntervalMs = pAI->EngagedUpdateIntervalMs;
        m_wanderRadius            = pAI->WanderRadius;
        m_wanderMinIntervalMs     = pAI->WanderMinIntervalMs;
        m_wanderMaxIntervalMs     = pAI->WanderMaxIntervalMs;
        // AIType 은 두뇌 선택용(현재 Fsm 만 구현, SpawnMonster 가 주입). BehaviorTree 는 나중.
    }

    // 이동속도: 종류 데이터의 MoveSpd 스탯에서 온다. 데이터에 없으면(Total<=0) 기본값으로 시드.
    // (MoveTo 는 GetStatTotal(EStatGroup::MoveSpd) 를 읽으므로, 0 이면 움직이지 못한다.)
    if (GetStatTotal(EStatGroup::MoveSpd) <= 0.0)
        m_statComponent.ApplyStat(EStat::MoveSpdAdd, k_defaultMoveSpeed);

    // 스킬을 GameData_Monster 의 SkillKey# 에서 읽어 채운다 (우선순위 = 순서).
    // 정적 스펙(사거리/쿨다운/윈드업/후딜/대미지계수)은 GameData_Skill 에서, 런타임 쿨다운만 MonsterSkill 이 보유.
    // v1 대미지 = DamageCoeff × 공격력(StrTotal, 없으면 1).
    const double atkPower = (GetStatTotal(EStatGroup::Str) > 0.0) ? GetStatTotal(EStatGroup::Str) : 1.0;
    const int32 skillKeyCount = m_pMonsterData->GetSkillKeyCount();
    for (int32 i = 0; i < skillKeyCount; ++i)
    {
        const int32 skillKey = m_pMonsterData->GetSkillKey(i);
        if (skillKey == 0)
            continue;   // 빈 슬롯

        const GameData_Skill* pSkill = GameDataTable_Skill::FindData(skillKey);
        if (pSkill == nullptr)
        {
            LOG_WRITE(LogLevel::Error, std::format("monster skill data not found. objectId={} skillKey={}", GetObjectId(), skillKey));
            continue;   // 데이터 누락 시 해당 ability 만 건너뛴다 (초기화 자체는 계속).
        }

        MonsterSkill skill;
        skill.skillId      = skillKey;               // = GameData_Skill.Key (AbilityCastNtf/SkillDamageNtf 에 실음)
        skill.range        = pSkill->MaxRange;
        skill.cooldownMs   = pSkill->CooldownMs;
        skill.castTimeMs   = pSkill->CastDelayMs;
        skill.actionLockMs = pSkill->ActionLockMs;
        skill.damage       = pSkill->DamageCoeff * atkPower;
        m_combat.AddSkill(skill);
    }

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

    // 전투 컴포넌트 진행(쿨다운 + 캐스트 페이즈) — integrate. 두뇌보다 먼저.
    // (윈드업 만료 시 여기서 효과가 발동되고, 두뇌는 GetCombat().IsCastBusy 동안 행동을 미룬다.)
    m_combat.Update(deltaMs);

    // 의사결정/행동은 두뇌에 위임.
    if (m_ai)
        m_ai->Update(*this, deltaMs);
}

// (타겟 탐색/해소(AcquireTarget/GetTarget/HasTarget/ClearTarget)는 MonsterCombatComponent 로 통합.)

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

void Monster::OnDamagedBy(int64 attackerObjectId)
{
    if (IsDead() || attackerObjectId == 0 || attackerObjectId == GetObjectId())
        return;

    // 이미 살아있는 교전 타겟이 있으면 교체하지 않는다(무교전일 때만 반격 → 다중 피격 thrash 방지).
    // GetTarget 은 매 tick Stage 에서 해소하므로, 타겟이 사라진 경우(nullptr)엔 새 공격자에 반응한다.
    if (m_combat.GetTarget() != nullptr)
        return;

    Stage* pStage = GetStage();
    if (pStage == nullptr)
        return;

    // 공격자가 살아있는 Character(캐릭터)일 때만 어그로. 환경/무효/이미 죽은 공격자는 무시.
    StageObject* pAttacker = pStage->FindObject(attackerObjectId);
    if (pAttacker == nullptr || pAttacker->GetObjectType() != EObjectType::Character)
        return;
    if (static_cast<ActorObject*>(pAttacker)->IsDead())
        return;

    // 공격자를 강제 타겟으로 삼고(어그로 범위 무시) 두뇌를 도발한다.
    m_combat.SetTarget(attackerObjectId);
    if (m_ai)
        m_ai->OnProvoked(*this);
}

// (스킬 선택 + 캐스트 생애주기(TryBeginCast/advanceCast/onCastStrike/CancelCast) + 효과 발동은
//  MonsterCombatComponent 로 이동했다. Monster::Update 가 m_combat.Update 로 진행, 두뇌는 GetCombat() 으로 호출.)

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
        // Monster 는 매 tick repath 가능하므로 이동 실패 로그는 끈다(스팸 방지).
        m_mover.SetDestination(*this, destX, destY, destZ, /*logMoveFailure*/ false);
        m_pathTargetX  = destX;
        m_pathTargetZ  = destZ;
        m_hasPathTarget = true;
    }

    // 이동속도는 MoveSpdTotal 스탯에서 (버프/디버프 자동 반영).
    m_mover.Update(*this, deltaMs, static_cast<float>(GetStatTotal(EStatGroup::MoveSpd)));

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
