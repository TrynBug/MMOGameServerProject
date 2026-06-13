#pragma once

#include "pch.h"
#include "StageObjects/StageObject.h"
#include "Components/BuffComponent.h"
#include "Components/SkillComponent.h"

#include "Enum/GameEnum_Stat.h"

// 전방선언: 스탯 컴포넌트 베이스 (파생이 GetStatComponent 로 노출)
class StatComponentBase;

// ─────────────────────────────────────────────────────────────
// ActorObject 베이스 클래스
// ─────────────────────────────────────────────────────────────
//
// StageObject 중에서 "상호작용 가능한 오브젝트" 의 공통 베이스 클래스이다.
// Character, Monster, Pet, NPC 등이 이 클래스를 상속받는다.
//
// ── 현재HP / 현재MP ──
// 최대치(HpTotal/MpTotal 스탯)와 달리, 현재HP/MP 는 대미지/회복으로만 변하는 런타임 상태다.
// 그래서 스탯 컴포넌트가 아니라 여기(ActorObject)에 직접 둔다.
// 최대치는 파생 클래스마다 보유 방식이 다르므로(Character=스탯 컴포넌트, Monster=경량) 
// 순수가상 GetMaxHp()/GetMaxMp() 로 얻는다.
//
// 단순 트리거(Trigger) 나 장식용 Prop 처럼 상호작용이 없는 오브젝트는
// 이 클래스를 상속받지 않고 StageObject 를 직접 상속받는다.
//
// 멤버 접근은 소속 Stage의 컨텐츠 스레드에서만 이루어진다.
// 그래서 별도의 락 없이 사용한다.
class ActorObject : public StageObject
{
public:
    // 생성자는 파라미터 없음. 단, 멤버 컴포넌트(Buff/Skill)는 owner(this)를 필요로 하므로
    // 생성자 초기화 리스트에서 this 로 연결한다(정의는 .cpp). 이는 멤버 구성이라 검증 대상이 아니다.
    ActorObject();
    ~ActorObject() override = default;

    ActorObject(const ActorObject&) = delete;
    ActorObject& operator=(const ActorObject&) = delete;

    // 파생 클래스의 Initialize 가 호출하는 부모 초기화 체인. StageObject::Initialize 로 위임.
    [[nodiscard]] bool Initialize(int64 objectId, EObjectType objectType);

public:
    // ── 최대 HP / MP ──────────────────────────────────────────
    // 파생 클래스가 자신의 스탯 보유 방식에 맞게 제공한다.
    // (Character 는 스탯 컴포넌트의 HpTotal/MpTotal, Monster 는 경량 스탯 등.)
    virtual double GetStatTotal(EStatGroup group) const = 0;

    // ── 스탯 컴포넌트 접근 ──────────────────────────────────────
    // 버프 등 스탯 소스가 ApplyStat/RemoveStat 하기 위해 사용한다.
    // 파생이 자신의 컴포넌트를 노출한다 (Character=CharacterStatComponent, Monster=BasicStatComponent).
    virtual StatComponentBase* GetStatComponent() = 0;

    // ── 버프 컴포넌트 ──────────────────────────────────────────
    BuffComponent&       GetBuffComponent()       { return m_buffComponent; }
    const BuffComponent& GetBuffComponent() const { return m_buffComponent; }

    // 스킬 컴포넌트 접근.
    SkillComponent&       GetSkillComponent()       { return m_skillComponent; }
    const SkillComponent& GetSkillComponent() const { return m_skillComponent; }

    // ── 버프에 의한 변화의 클라 통지 훅 ─────────────────────────
    // 버프가 스탯을 바꾸면 OnStatsChangedByBuff(), DoT/HoT 로 HP 가 바뀌면 OnHpChangedByBuff() 가 호출된다.
    // 파생이 자신의 방식으로 통지한다 (Character=StatUpdateNtf/HpMpNtf, Monster=무시).
    virtual void OnStatsChangedByBuff() {}
    virtual void OnHpChangedByBuff() {}

    // 이동기(Mobility)/이동 페이즈가 발동될 때 SkillComponent 가 호출한다.
    // 파생이 자신의 이동 방식으로 구현한다 (Character=대시/순간이동, Monster=무시). 기본은 no-op. (5d 예정.)
    virtual void ApplySkillMovement(const Vector3& dir, float distance, int32 durationMs) {}

    // 스킬 시전 액션락(시전 중 이동 불가)을 적용한다. SkillComponent 가 Stationary 시전 시 호출.
    // 파생이 자기 방식으로 구현 (Character=현재 이동 정지+잠금 카운트다운, Monster=무시 — FSM이 시전 중 정지).
    virtual void ApplyActionLock(int64 lockMs) {}

    // 그룹의 총합 스탯값(예: EStatGroup::Hp → HpTotal)을 리턴한다. 파생이 제공.
    //   - Character : CharacterStatComponent 의 해당 Total 스탯
    //   - Monster   : BasicStatComponent::GetTotal(group)
    // 최대치는 HpTotal/MpTotal 총합 스탯이므로 GetStatTotal 위에서 공통 구현한다.
    double GetMaxHp() const { return GetStatTotal(EStatGroup::Hp); }
    double GetMaxMp() const { return GetStatTotal(EStatGroup::Mp); }

    // ── 현재 HP / MP ──────────────────────────────────────────
    double GetCurHp() const { return m_curHp; }
    double GetCurMp() const { return m_curMp; }

    // [0, 최대치] 범위로 clamp 하여 설정한다.
    void SetCurHp(double hp) { m_curHp = clampToRange(hp, GetMaxHp()); }
    void SetCurMp(double mp) { m_curMp = clampToRange(mp, GetMaxMp()); }

    // 현재 HP/MP 를 각각의 최대치로 채운다. (생성 시 풀피 등)
    void FillHp() { m_curHp = GetMaxHp(); }
    void FillMp() { m_curMp = GetMaxMp(); }

    // ── 사망 상태 ────────────────────────────────
    // 사망은 명시적 상태다(HP 0 파생이 아님). 대미지가 아닌 사인(스크립트/지형 등)도 있을 수 있어,
    // HP 와 무관하게 MarkDead 로 1회 전환한다. 디스폰은 이 상태를 기준으로 별도 스케줄링된다.
    bool  IsDead() const { return m_isDead; }
    int64 GetKillerObjectId() const { return m_killerObjectId; }

    // 사망으로 전환한다. 이미 사망 상태면 아무것도 하지 않고 false (중복 사망 방지).
    // 이번 호출에서 처음 사망 전환했으면 true (호출자가 사망 통보를 1회만 보내도록).
    bool MarkDead(int64 killerObjectId)
    {
        if (m_isDead)
            return false;
        m_isDead         = true;
        m_killerObjectId = killerObjectId;
        return true;
    }

private:
    // value 를 [0, maxValue] 로 clamp. maxValue 가 음수면 0 으로 본다.
    static double clampToRange(double value, double maxValue)
    {
        const double hi = (maxValue > 0.0) ? maxValue : 0.0;
        if (value < 0.0)   return 0.0;
        if (value > hi)    return hi;
        return value;
    }

private:
    // 현재 HP / MP. 최대치는 GetMaxHp()/GetMaxMp() 로 얻는다.
    double m_curHp = 0.0;
    double m_curMp = 0.0;

    // 사망 상태 (MarkDead 로 전환). m_killerObjectId 0 = 처치자 없음.
    bool  m_isDead = false;
    int64 m_killerObjectId = 0;

    // 버프 컴포넌트. 생성자에서 owner(this)로 초기화된다.
    BuffComponent m_buffComponent;

    // 스킬 컴포넌트. 생성자에서 owner(this)로 초기화된다.
    SkillComponent m_skillComponent;
};

using ActorObjectPtr  = std::shared_ptr<ActorObject>;
using ActorObjectWPtr = std::weak_ptr<ActorObject>;
