#include "pch.h"
#include "BuffComponent.h"

#include "ActorObject.h"
#include "Stage.h"
#include "StatComponentBase.h"

#include "Generated/GameData_Buff.h"
#include "Enum/GameEnum_Buff.h"

#include <vector>

void BuffComponent::ApplyBuff(int32 buffKey, int64 casterObjectId)
{
    const GameData_Buff* pData = GameDataTable_Buff::FindData(buffKey);
    if (pData == nullptr)
    {
        LOG_WRITE(LogLevel::Error, std::format("BuffComponent::ApplyBuff - unknown buffKey={} objectId={}",
            buffKey, m_pOwner ? m_pOwner->GetObjectId() : 0));
        return;
    }

    const int64 freshRemain = (pData->DurationMs < 0) ? -1 : pData->DurationMs;

    // ── 이미 적용된 버프면 StackPolicy 에 따라 처리 ──────────────
    auto iter = m_buffs.find(buffKey);
    if (iter != m_buffs.end())
    {
        BuffInstance& buff = iter->second;

        switch (pData->StackPolicy)
        {
        case EBuffStackPolicy::Ignore:
            return;   // 이미 있으면 아무것도 안 함.

        case EBuffStackPolicy::Refresh:
            buff.remainMs       = freshRemain;
            buff.casterObjectId = casterObjectId;   // 최신 시전자로 갱신
            broadcastBuffNtf(buff);
            return;

        case EBuffStackPolicy::Stack:
            buff.remainMs       = freshRemain;       // 스택 시 지속시간도 갱신
            buff.casterObjectId = casterObjectId;
            if (buff.stackCount < static_cast<int32>(pData->MaxStack))
            {
                buff.stackCount += 1;
                if (changeStatBy(buff, +1))          // 추가 1스택분 스탯
                    m_pOwner->OnStatsChangedByBuff();
            }
            broadcastBuffNtf(buff);
            return;

        default:
            return;
        }
    }

    // ── 신규 적용 ──────────────────────────────────────────────
    BuffInstance newBuff;
    newBuff.pData          = pData;
    newBuff.stackCount     = 1;
    newBuff.remainMs       = freshRemain;
    newBuff.tickAccumMs    = 0;
    newBuff.casterObjectId = casterObjectId;

    auto [insertedIter, _] = m_buffs.emplace(buffKey, newBuff);
    BuffInstance& buff = insertedIter->second;

    if (changeStatBy(buff, +1))
        m_pOwner->OnStatsChangedByBuff();

    broadcastBuffNtf(buff);
}

void BuffComponent::RemoveBuff(int32 buffKey)
{
    auto iter = m_buffs.find(buffKey);
    if (iter == m_buffs.end())
        return;

    // 현재 스택 전체만큼 스탯 역적용.
    const bool statChanged = changeStatBy(iter->second, -iter->second.stackCount);

    m_buffs.erase(iter);

    if (statChanged)
        m_pOwner->OnStatsChangedByBuff();

    broadcastBuffRemove(buffKey);
}

void BuffComponent::Update(int64 deltaMs)
{
    if (m_buffs.empty())
        return;

    // 만료된 버프 key 수집 (순회 중 컨테이너 변경 금지 → 순회 후 RemoveBuff).
    std::vector<int32> expiredKeys;

    for (auto& [buffKey, buff] : m_buffs)
    {
        // ── 주기효과(DoT/HoT) 틱 ──
        const int64 interval = buff.pData->TickIntervalMs;
        if (interval > 0 && buff.pData->PeriodicType != EPeriodicEffect::None)
        {
            buff.tickAccumMs += deltaMs;
            // deltaMs 가 interval 보다 크면(오브젝트 주기가 길면) 놓친 틱을 모두 발사.
            while (buff.tickAccumMs >= interval)
            {
                fireTick(buff);
                buff.tickAccumMs -= interval;
            }
        }

        // ── 지속시간 ── (-1 = 영구는 만료 안 함)
        if (buff.remainMs >= 0)
        {
            buff.remainMs -= deltaMs;
            if (buff.remainMs <= 0)
                expiredKeys.push_back(buffKey);
        }
    }

    for (int32 key : expiredKeys)
        RemoveBuff(key);
}

void BuffComponent::OnOwnerDead()
{
    if (m_buffs.empty())
        return;

    std::vector<int32> toRemove;
    for (const auto& [buffKey, buff] : m_buffs)
    {
        if (buff.pData->RemoveOnDeath)
            toRemove.push_back(buffKey);
    }
    for (int32 key : toRemove)
        RemoveBuff(key);
}

void BuffComponent::OnOwnerStageChange()
{
    if (m_buffs.empty())
        return;

    std::vector<int32> toRemove;
    for (const auto& [buffKey, buff] : m_buffs)
    {
        if (buff.pData->RemoveOnStageChange)
            toRemove.push_back(buffKey);
    }
    for (int32 key : toRemove)
        RemoveBuff(key);
}

void BuffComponent::ForEachBuff(const std::function<void(int32, int32, int32)>& callback) const
{
    for (const auto& [buffKey, buff] : m_buffs)
    {
        const int32 remainMs = (buff.remainMs < 0) ? -1 : static_cast<int32>(buff.remainMs);
        callback(buffKey, buff.stackCount, remainMs);
    }
}

bool BuffComponent::changeStatBy(const BuffInstance& buff, int32 stacksDelta)
{
    StatComponentBase* pStat = m_pOwner->GetStatComponent();
    if (pStat == nullptr || stacksDelta == 0)
        return false;

    const GameData_Buff* pData = buff.pData;
    const bool  bApply = (stacksDelta > 0);
    const int32 count  = bApply ? stacksDelta : -stacksDelta;

    bool changed = false;
    const int32 statCount = pData->GetStatCount();
    for (int32 s = 0; s < count; ++s)
    {
        for (int32 i = 0; i < statCount; ++i)
        {
            const EStat stat = pData->GetStat(i);
            if (stat == EStat::None)
                continue;

            // 버프 스탯은 구성요소 스탯(*Add/*AddPct/*Amp/*Reduce)이어야 한다.
            // Total 스탯은 StatComponentBase 가 자동 재계산하므로 직접 넣으면 거부됨(에러 로그).
            const double value = pData->GetStatValue(i);
            if (bApply)
                pStat->ApplyStat(stat, value);
            else
                pStat->RemoveStat(stat, value);
            changed = true;
        }
    }
    return changed;
}

void BuffComponent::fireTick(const BuffInstance& buff)
{
    const double amount = buff.pData->PeriodicValue * buff.stackCount;

    switch (buff.pData->PeriodicType)
    {
    case EPeriodicEffect::DamageHp:
        // 대미지는 Stage::ApplyEffectDamage 로 통일한다 (HP 감소 + SkillDamageNtf + 사망 판정/통보).
        // casterObjectId 가 처치자(killer)로 사망 패킷에 실린다. DoT 가 죽이면 여기서 ObjectDeathNtf 까지 나간다.
        if (Stage* pStage = m_pOwner->GetStage())
            pStage->ApplyEffectDamage(*m_pOwner, amount, buff.casterObjectId);
        break;

    case EPeriodicEffect::HealHp:
        m_pOwner->SetCurHp(m_pOwner->GetCurHp() + amount);
        m_pOwner->OnHpChangedByBuff();
        break;

    default:
        break;
    }
}

void BuffComponent::broadcastBuffNtf(const BuffInstance& buff)
{
    Stage* pStage = m_pOwner->GetStage();
    if (pStage == nullptr)
        return;

    const int32 remainMs = (buff.remainMs < 0) ? -1 : static_cast<int32>(buff.remainMs);
    pStage->BroadcastBuffNtf(*m_pOwner, buff.pData->Key, buff.stackCount, remainMs);
}

void BuffComponent::broadcastBuffRemove(int32 buffKey)
{
    Stage* pStage = m_pOwner->GetStage();
    if (pStage == nullptr)
        return;

    pStage->BroadcastBuffRemoveNtf(*m_pOwner, buffKey);
}
