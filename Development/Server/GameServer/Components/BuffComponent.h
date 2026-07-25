#pragma once

#include "pch.h"
#include "Components/BuffInstance.h"

#include <functional>
#include <unordered_map>

// 전방선언
class ActorObject;

// ─────────────────────────────────────────────────────────────
// BuffComponent
// ─────────────────────────────────────────────────────────────
//
// 액터(Character/Monster)가 멤버로 소유하는 버프 컴포넌트.
// 버프의 적용/해제/만료/스택/주기효과(DoT/HoT)를 전적으로 서버에서 처리한다.
//
// ── 인스턴스 정책 (v1) ──
// 액터당 buffKey 1인스턴스. 서로 다른 시전자가 같은 버프를 걸어도 1개로 합쳐진다.
// (시전자별 독립 인스턴스는 향후 확장.)
//
// ── 액터 종류 비의존 ──
// 이 컴포넌트는 owner 가 Character 인지 Monster 인지 모른다. 스탯 적용은 ActorObject 가
// 노출하는 StatComponentBase 를 통하고, 스탯/HP 변화의 클라 통지는 ActorObject 의
// OnStatsChangedByBuff()/OnHpChangedByBuff() 훅에 위임한다(패킷 타입을 모름).
// 버프 뱃지(BuffNtf/BuffRemoveNtf)는 소속 Stage 의 AOI 브로드캐스트로 전송된다.
//
// ── 스레드 ──
// 소속 Stage 의 컨텐츠 스레드에서만 접근한다 (Update 포함). 별도 락 없음.
class BuffComponent
{
public:
    explicit BuffComponent(ActorObject* pOwner) : m_pOwner(pOwner) {}

    BuffComponent(const BuffComponent&) = delete;
    BuffComponent& operator=(const BuffComponent&) = delete;

    // 버프 적용. 이미 있으면 StackPolicy(Refresh/Stack/Ignore)에 따라 처리한다.
    // casterObjectId 0 = 시전자 없음(환경 효과 등).
    void ApplyBuff(int32 buffKey, int64 casterObjectId = 0);

    // 버프 제거 (만료/디스펠/정리 공통). 없으면 no-op.
    void RemoveBuff(int32 buffKey);

    // 매 tick(액터 Update 안에서) 호출. 주기효과 틱 + 만료 처리.
    // deltaMs 는 마지막 Update 이후 누적 경과시간.
    void Update(int64 deltaMs);

    // 사망 시 RemoveOnDeath 버프 일괄 제거 (death 시스템 연동 시 호출).
    void OnOwnerDead();

    // Stage 이동 시 RemoveOnStageChange 버프 일괄 제거.
    void OnOwnerStageChange();

    // spawn 스냅샷 채우기용. (buffKey, stackCount, remainMs[-1=영구]) 콜백.
    void ForEachBuff(const std::function<void(int32 buffKey, int32 stackCount, int32 remainMs)>& callback) const;

    bool Empty() const { return m_buffs.empty(); }
    size_t GetBuffCount() const { return m_buffs.size(); }

private:
    // 버프 1개의 스탯 효과를 적용/역적용. stacksDelta>0 이면 그만큼 스택을 ApplyStat,
    // <0 이면 그만큼 RemoveStat. Amp/Reduce 는 비선형이라 스택마다 1회씩 호출한다.
    // 스탯을 1개라도 변경했으면 true (호출자가 OnStatsChangedByBuff 여부 판단).
    bool changeStatBy(const BuffInstance& buff, int32 stacksDelta);

    // 주기효과(DoT/HoT) 1틱 발사. HP 변경 후 OnHpChangedByBuff 통지.
    void fireTick(const BuffInstance& buff);

    // 버프 뱃지 추가/갱신, 제거 브로드캐스트 (Stage AOI 경유).
    void broadcastBuffNtf(const BuffInstance& buff);
    void broadcastBuffRemove(int32 buffKey);

private:
    ActorObject* m_pOwner = nullptr;
    std::unordered_map<int32, BuffInstance> m_buffs;   // key = buffKey
};
