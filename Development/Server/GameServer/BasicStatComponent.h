#pragma once

#include "pch.h"

#include "StatComponentBase.h"
#include "Enum/GameEnum_Stat.h"          // EStat, EStatGroup
#include "Generated/GameData_Stat.h"     // GameDataTable_Stat (그룹→Total 스탯 변환)

#include <functional>
#include <unordered_map>

// ─────────────────────────────────────────────────────────────
// BasicStatComponent
// ─────────────────────────────────────────────────────────────
//
// Character 를 제외한, 스탯을 쓰는 단순 액터(Monster, Pet 등)가 소유하는 경량 스탯 컴포넌트.
// (전투하지 않는 NPC/Prop 등은 스탯 컴포넌트를 갖지 않는다.)
//
// ── 저장 구조 (캐릭터와의 차이) ──
//   - raw 스탯 : unordered_map<EStat, double>. 희소 저장.
//                몬스터는 보통 HpAdd/MoveSpdAdd/PDefAdd 같은 소수의 스탯만 가지며,
//                버프/디버프로 HpAmp 등이 런타임에 추가될 수 있다. 그래서 희소 맵이 적합.
//   - 총합     : double[EStatGroup::Max]. 그룹 인덱스로 직접 접근하는 작은 조밀 배열.
//                핫패스(이동/대미지)가 자주 읽으므로 O(1) 배열로 캐시.
//
// raw 갱신/총합 재계산은 버프 시점에만 일어나며(드물다) 그때만 맵을 조회한다.
// 핫패스 총합 읽기(GetTotal)는 배열만 읽으므로 맵을 건드리지 않는다.
//
// 공식/누적/합성 흐름은 StatComponentBase 가 담당한다.
//
// ── 스레드 ──
// 소속 Stage 의 컨텐츠 스레드에서만 접근한다. 별도 락 없음.
class BasicStatComponent : public StatComponentBase
{
public:
    // ── 총합 읽기 (핫패스, O(1)) ──
    // 그룹 인덱스로 총합 캐시를 직접 읽는다. 유효하지 않은 그룹이면 0.
    double GetTotal(EStatGroup group) const
    {
        const size_t idx = static_cast<size_t>(group);
        if (idx >= static_cast<size_t>(EStatGroup::Max))
            return 0.0;
        return m_totals[idx];
    }

protected:
    // ── StatComponentBase 저장 접근 구현 ──
    double getRaw(EStat stat) const override
    {
        if (stat == EStat::None)
            return 0.0;
        auto iter = m_rawStats.find(stat);
        if (iter == m_rawStats.cend())
            return 0.0;   // 없는 raw 는 0 (슬롯 미보유)
        return iter->second;
    }

    void setRaw(EStat stat, double value) override
    {
        m_rawStats[stat] = value;
    }

    void setTotal(EStatGroup group, double value) override
    {
        const size_t idx = static_cast<size_t>(group);
        if (idx >= static_cast<size_t>(EStatGroup::Max))
            return;
        m_totals[idx] = value;
    }

    // raw 맵과 총합 배열을 둘 다 순회한다(분리 저장이므로).
    // 총합 배열은 그룹 인덱스라, 그룹→Total 스탯(EStat)으로 변환해 콜백한다. 0 필터는 베이스가 적용.
    void forEachStoredStat(const std::function<void(EStat, double)>& callback) const override
    {
        // raw 스탯 (희소 맵)
        for (const auto& pair : m_rawStats)
            callback(pair.first, pair.second);

        // 총합 스탯 (그룹 배열 → Total 스탯로 변환)
        for (size_t i = 1; i < static_cast<size_t>(EStatGroup::Max); ++i)
        {
            const StatGroupInfo* pInfo = GameDataTable_Stat::GetGroupInfo(static_cast<EStatGroup>(i));
            if (pInfo == nullptr || pInfo->total == EStat::None)
                continue;
            callback(pInfo->total, m_totals[i]);
        }
    }

private:
    // raw 스탯: 희소 저장. 버프로 추가될 때만 늘어난다.
    std::unordered_map<EStat, double> m_rawStats;

    // 총합 캐시: 그룹 인덱스. 핫패스가 읽는다. 초기 전부 0.
    double m_totals[static_cast<size_t>(EStatGroup::Max)] = {};
};
