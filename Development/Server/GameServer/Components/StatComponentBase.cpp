#include "pch.h"
#include "Components/StatComponentBase.h"

#include <algorithm>   // std::clamp
#include <cmath>       // std::abs

namespace
{
    // 퍼센트 -> 배수 변환 계수.
    constexpr double k_pctToRatio = 0.01;

    // 0 으로 간주할 임계값. 스탯은 double 누적이라 미세 오차가 남을 수 있어 정확히 0 비교 대신 사용.
    constexpr double k_zeroEpsilon = 1e-9;
}

double StatComponentBase::applyOp(EStatOp op, double cur, double value, bool bAdd)
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

void StatComponentBase::ApplyStat(EStat stat, double value)
{
    changeStat(stat, value, true);
}

void StatComponentBase::RemoveStat(EStat stat, double value)
{
    changeStat(stat, value, false);
}

void StatComponentBase::ForEachNonZeroStat(const std::function<void(EStat, double)>& callback) const
{
    // 파생의 저장소를 순회하되, 0(오차 포함) 항목은 거른다.
    forEachStoredStat([&callback](EStat stat, double value)
    {
        if (std::abs(value) > k_zeroEpsilon)
            callback(stat, value);
    });
}

void StatComponentBase::changeStat(EStat stat, double value, bool bAdd)
{
    const GameData_Stat* pData = GameDataTable_Stat::FindDataByStat(stat);
    if (pData == nullptr)
    {
        LOG_WRITE(LogLevel::Error, std::format("unknown stat. (Stat={})", static_cast<int>(stat)));
        return;
    }

    if (pData->StatOp == EStatOp::Total)
    {
        LOG_WRITE(LogLevel::Error, std::format("cannot change a Total stat directly. (Stat={})", static_cast<int>(stat)));
        return;
    }

    double newRaw = applyOp(pData->StatOp, getRaw(stat), value, bAdd);

    // 곱연산/점감곱연산 raw 는 ×0 미만으로 가면 이후 역연산 분모가 무너지므로 하한 clamp.
    if (pData->StatOp == EStatOp::Amp || pData->StatOp == EStatOp::Reduce)
        newRaw = (std::max)(newRaw, pData->MinApplyValue);

    setRaw(stat, newRaw);

    recomputeTotal(pData->StatGroup);
}

void StatComponentBase::recomputeTotal(EStatGroup group)
{
    const StatGroupInfo* pInfo = GameDataTable_Stat::GetGroupInfo(group);
    if (pInfo == nullptr || pInfo->total == EStat::None)
        return;   // 총합이 없는 그룹이면 재계산할 대상 없음.

    // 슬롯이 데이터에 없으면(None) getRaw 가 0 을 리턴 → 해당 곱셈 항이 1 이 되어 무영향.
    const double add    = getRaw(pInfo->slot[static_cast<size_t>(EStatOp::Add)]);
    const double addPct = getRaw(pInfo->slot[static_cast<size_t>(EStatOp::AddPct)]);
    const double amp    = getRaw(pInfo->slot[static_cast<size_t>(EStatOp::Amp)]);
    const double reduce = getRaw(pInfo->slot[static_cast<size_t>(EStatOp::Reduce)]);

    double total = add
        * (1.0 + addPct * k_pctToRatio)
        * (1.0 + amp    * k_pctToRatio)
        * (1.0 - reduce * k_pctToRatio);

    // 최종 적용값은 Total 스탯의 Min/MaxApplyValue 로 clamp.
    const GameData_Stat* pTotalData = GameDataTable_Stat::FindDataByStat(pInfo->total);
    if (pTotalData != nullptr)
        total = std::clamp(total, pTotalData->MinApplyValue, pTotalData->MaxApplyValue);

    setTotal(group, total);
}
