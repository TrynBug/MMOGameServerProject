#pragma once

#include "pch.h"

#include <vector>

// 전방선언: owner(Monster) 와 타겟. 완전타입은 MonsterCombatComponent.cpp 에서만 필요.
class Monster;
class StageObject;

// ─────────────────────────────────────────────────────────────
// MonsterSkill : 몬스터가 사용하는 스킬 1개의 런타임 정보
// ─────────────────────────────────────────────────────────────
// 정적 스펙(사거리/쿨다운/윈드업/후딜/대미지)은 GameData_Skill 에서 읽어(Monster::Initialize) 채우고,
// 여기에는 런타임 상태(remainingCooldownMs)만 함께 보관한다.
struct MonsterSkill
{
    int32  skillId             = 0;      // GameData_Skill.Key (통보/대미지 패킷에 실리는 skill_key)
    float  range               = 0.0f;   // 사용 가능 사거리 (이 거리 이내 타겟)
    int64  cooldownMs          = 0;      // 쿨다운 (ms)
    int64  castTimeMs          = 0;      // 선딜/윈드업 시간 (ms). 0 이면 즉시 발동.
    int64  actionLockMs        = 0;      // 후딜/회복 시간 (ms). 발동 후 다음 행동까지 잠금(=반격 창).
    int64  remainingCooldownMs = 0;      // 남은 쿨다운 (ms, 런타임)
    double damage              = 0.0;    // 적중 시 대미지 (서버 권위).
};

// ─────────────────────────────────────────────────────────────
// EMonsterCastPhase : 캐스트 페이즈
// ─────────────────────────────────────────────────────────────
//   None     : 캐스트 중 아님.
//   Windup   : 선딜(윈드업). 이 동안 이동/재조준 없이 잠금. 만료 시 효과 발동.
//   Recovery : 후딜(회복). 발동 후 다음 행동까지 잠금(=플레이어 반격 창).
enum class EMonsterCastPhase { None, Windup, Recovery };

// ─────────────────────────────────────────────────────────────
// MonsterCombatComponent — 몬스터 전투(타겟팅 + 스킬 + 캐스트 생애주기)
// ─────────────────────────────────────────────────────────────
//
// "이 몬스터가 누구와 싸우고(타겟팅), 어떤 스킬을 보유하고 어떻게 시전하는가"(몸체의 능력/상태)를 담는다.
// 의사결정(FSM/BT 두뇌)은 owner(Monster)를 통해 SelectReadySkill/TryBeginCast 등을 호출할 뿐,
// 윈드업/발동/회복/쿨다운/통보의 진행은 전적으로 이 컴포넌트가 단일 지점에서 처리한다.
// → FSM/BT 어느 두뇌든 같은 전투 코드를 중복 없이 재사용한다.
//
// owner(Monster*)는 BuffComponent 와 동일한 패턴의 백포인터다. 위치/objectId/Stage/회전/이동은
// owner 를 통해 접근한다 (이 컴포넌트는 owner 가 어떤 종류 몬스터인지 알 필요 없다).
//
// 스레드: owner 의 컨텐츠 스레드에서만 접근(Update 포함). 락 없음.
class MonsterCombatComponent
{
public:
    explicit MonsterCombatComponent(Monster* pOwner) : m_pOwner(pOwner) {}

    MonsterCombatComponent(const MonsterCombatComponent&)            = delete;
    MonsterCombatComponent& operator=(const MonsterCombatComponent&) = delete;

    // ── 타겟팅(perception): 누구와 싸우는가 ──
    // 주변 sector 에서 어그로 범위(owner->GetAggroRange) 내 가장 가까운 유저를 현재 타겟으로 설정(없으면 해제).
    void         AcquireTarget();
    // 현재 타겟을 Stage 에서 해소. 사라졌거나 없으면 nullptr (despawn 안전 위해 objectId 로 보관).
    StageObject* GetTarget() const;
    bool         HasTarget() const { return m_targetObjectId != 0; }
    void         ClearTarget()      { m_targetObjectId = 0; }
    // 강제 타겟 지정(어그로 범위 무시). 피격 반격 등 perception 외 경로에서 사용.
    // GetTarget 이 매 tick Stage 에서 해소하므로, 사라진 대상이어도 안전(다음 tick nullptr).
    void         SetTarget(int64 objectId) { m_targetObjectId = objectId; }

    // ── 스킬 보유 (Monster::Initialize 가 채운다) ──
    void                AddSkill(const MonsterSkill& skill) { m_skills.push_back(skill); }
    int32               GetSkillCount() const { return static_cast<int32>(m_skills.size()); }
    const MonsterSkill& GetSkill(int32 index) const { return m_skills[index]; }

    // 공격 사거리 = 보유 스킬들의 최대 사거리. 근접몹=짧고 원거리몹=길다(역할별 스킬 배정 전제).
    float GetMaxAttackRange() const
    {
        float r = 0.0f;
        for (const MonsterSkill& s : m_skills)
            if (s.range > r) r = s.range;
        return r;
    }
    // 사용 가능한(쿨다운 끝 + AI별 사거리 조건) 스킬 중 우선순위(목록 순서) 최상의 인덱스. 없으면 -1.
    int32 SelectReadySkill(float distToTarget) const;

    // 매 tick(owner Update 안에서, 두뇌보다 먼저) 호출. 쿨다운 진행 + 캐스트 페이즈 진행.
    void Update(int64 deltaMs);

    // ── 캐스트(두뇌의 단일 진입점) ──
    // skillIndex 시전을 "시작"한다. 캐스트/회복 중·쿨다운 미회복·AI별 사거리 밖·인덱스/타겟 무효면 false.
    // 성공 시: 타겟/방향/origin 커밋 + owner 회전·정지 + Windup 진입 + AbilityCastNtf 통보.
    [[nodiscard]] bool TryBeginCast(int32 skillIndex, StageObject* pTarget);
    bool IsCasting()    const { return m_castPhase == EMonsterCastPhase::Windup; }
    bool IsInRecovery() const { return m_castPhase == EMonsterCastPhase::Recovery; }
    bool IsCastBusy()   const { return m_castPhase != EMonsterCastPhase::None; }  // 이동/새 캐스트 잠금
    // 시전/회복 강제 취소(자원 환불 없음). 사망/CC 시 호출.
    void CancelCast();

private:
    void tickCooldowns(int64 deltaMs);
    void advanceCast(int64 deltaMs);
    void onCastStrike();
    void executeSkill(int32 index, StageObject* pTarget);   // EffectDamage 별 발동(Area/투사체/직격)
    void startCooldown(int32 index);

private:
    Monster* m_pOwner = nullptr;

    int64 m_targetObjectId = 0;   // 현재 타겟 (0=없음). 매 tick GetTarget 으로 해소(despawn 안전).

    std::vector<MonsterSkill> m_skills;

    // ── 캐스트 상태(커밋 스냅샷) ──
    EMonsterCastPhase m_castPhase      = EMonsterCastPhase::None;
    int32 m_castSkillIndex             = -1;
    int64 m_castRemainingMs            = 0;
    int64 m_castTargetObjectId         = 0;
    float m_castOriginX = 0.0f, m_castOriginY = 0.0f, m_castOriginZ = 0.0f;
    float m_castDirX = 0.0f, m_castDirZ = 0.0f;
};
