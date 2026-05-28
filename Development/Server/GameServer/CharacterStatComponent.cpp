#include "pch.h"
#include "CharacterStatComponent.h"

#include <algorithm>   // std::clamp

namespace
{
    // 퍼센트 -> 배수 변환 계수.
    constexpr double k_pctToRatio = 0.01;
}

double CharacterStatComponent::applyOp(EStatOp op, double cur, double value, bool bAdd)
{
    switch (op)
    {
    case EStatOp::Add:
    case EStatOp::AddPct:
        // 합연산: 단순 덧셈 누적. (AddPct 도 누적은 덧셈, 총합 합성 단계에서만 % 로 쓰임)
        return bAdd ? (cur + value) : (cur - value);

    case EStatOp::Amp:
        // 곱연산.
        //   더할 때: value + cur * (1 + value*0.01)
        //   뺄 때:   (cur - value) / (1 + value*0.01)
        if (bAdd)
            return value + cur * (1.0 + value * k_pctToRatio);
        else
            return (cur - value) / (1.0 + value * k_pctToRatio);

    case EStatOp::Reduce:
        // 점감곱연산.
        //   더할 때: 100 - (100 - cur) * (1 - value*0.01)
        //   뺄 때:   (cur - value) / (1 - value*0.01)
        if (bAdd)
            return 100.0 - (100.0 - cur) * (1.0 - value * k_pctToRatio);
        else
            return (cur - value) / (1.0 - value * k_pctToRatio);

    default:
        // Total/None 은 직접 누적 대상이 아니다.
        return cur;
    }
}

void CharacterStatComponent::ApplyStat(EStat stat, double value)
{
    const GameData_Stat* pData = GameDataTable_Stat::FindDataByStat(stat);
    if (pData == nullptr)
    {
        LOG_WRITE(LogLevel::Error, std::format("CharacterStatComponent::ApplyStat - unknown stat. (Stat={})", static_cast<int>(stat)));
        return;
    }
    if (pData->StatOp == EStatOp::Total)
    {
        LOG_WRITE(LogLevel::Error, std::format("CharacterStatComponent::ApplyStat - cannot apply a Total stat directly. (Stat={})", static_cast<int>(stat)));
        return;
    }

    const size_t idx = static_cast<size_t>(stat);
    m_stats[idx] = applyOp(pData->StatOp, m_stats[idx], value, true);

    // 곱연산/점감곱연산 raw 는 ×0 미만으로 가면 이후 역연산 분모가 무너지므로 하한 clamp.
    if (pData->StatOp == EStatOp::Amp || pData->StatOp == EStatOp::Reduce)
        m_stats[idx] = (std::max)(m_stats[idx], pData->MinApplyValue);

    recomputeTotal(pData->StatGroup);
}

void CharacterStatComponent::RemoveStat(EStat stat, double value)
{
    const GameData_Stat* pData = GameDataTable_Stat::FindDataByStat(stat);
    if (pData == nullptr)
    {
        LOG_WRITE(LogLevel::Error, std::format("CharacterStatComponent::RemoveStat - unknown stat. (Stat={})", static_cast<int>(stat)));
        return;
    }
    if (pData->StatOp == EStatOp::Total)
    {
        LOG_WRITE(LogLevel::Error, std::format("CharacterStatComponent::RemoveStat - cannot remove a Total stat directly. (Stat={})", static_cast<int>(stat)));
        return;
    }

    const size_t idx = static_cast<size_t>(stat);
    m_stats[idx] = applyOp(pData->StatOp, m_stats[idx], value, false);

    if (pData->StatOp == EStatOp::Amp || pData->StatOp == EStatOp::Reduce)
        m_stats[idx] = (std::max)(m_stats[idx], pData->MinApplyValue);

    recomputeTotal(pData->StatGroup);
}

void CharacterStatComponent::recomputeTotal(EStatGroup group)
{
    const StatGroupInfo* pInfo = GameDataTable_Stat::GetGroupInfo(group);
    if (pInfo == nullptr || pInfo->total == EStat::None)
        return;   // 총합이 없는 그룹이면 재계산할 대상 없음.

    // 슬롯이 데이터에 없으면 raw 0 으로 간주 → 해당 곱셈 항이 1 이 되어 무영향.
    const double add    = rawOrZero(pInfo->slot[static_cast<size_t>(EStatOp::Add)]);
    const double addPct = rawOrZero(pInfo->slot[static_cast<size_t>(EStatOp::AddPct)]);
    const double amp    = rawOrZero(pInfo->slot[static_cast<size_t>(EStatOp::Amp)]);
    const double reduce = rawOrZero(pInfo->slot[static_cast<size_t>(EStatOp::Reduce)]);

    double total = add
        * (1.0 + addPct * k_pctToRatio)
        * (1.0 + amp    * k_pctToRatio)
        * (1.0 - reduce * k_pctToRatio);

    // 최종 적용값은 Total 스탯의 Min/MaxApplyValue 로 clamp.
    const GameData_Stat* pTotalData = GameDataTable_Stat::FindDataByStat(pInfo->total);
    if (pTotalData != nullptr)
        total = std::clamp(total, pTotalData->MinApplyValue, pTotalData->MaxApplyValue);

    m_stats[static_cast<size_t>(pInfo->total)] = total;
}
