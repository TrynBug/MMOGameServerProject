#pragma once

#include "pch.h"

#include "Components/StatComponentBase.h"
#include "Enum/GameEnum_Stat.h"
#include "Generated/GameData_Stat.h"

#include <functional>

// ─────────────────────────────────────────────────────────────
// CharacterStatComponent
// ─────────────────────────────────────────────────────────────
//
// Character 가 멤버로 소유하는 스탯 컴포넌트. (몬스터 등은 BasicStatComponent 사용)
//
// ── 저장 구조 ──
// m_stats[EStat::Max] 단일 조밀 배열에 raw 누적값과 총합값을 모두 저장한다. (인덱스 = EStat 값)
// 캐릭터는 모든 스탯을 가질 수 있으므로 조밀 배열이 적합하다.
//
// 공식/누적/합성 흐름은 StatComponentBase 가 담당한다.
//
// ── 스레드 ──
// 소속 Stage 의 컨텐츠 스레드에서만 접근한다. 별도 락 없음.
class CharacterStatComponent : public StatComponentBase
{
public:
    // ── 임의 스탯 읽기 (캐릭터 전용, O(1)) ──
    // raw/총합 구분 없이 EStat 인덱스로 직접 읽는다. 유효하지 않으면 0.
    // (핫패스 총합 읽기는 ActorObject::GetStatTotal 을 권장. 이 함수는 raw 조회 등 캐릭터 내부용.)
    double Get(EStat stat) const
    {
        const size_t idx = static_cast<size_t>(stat);
        if (idx >= static_cast<size_t>(EStat::Max))
            return 0.0;
        return m_stats[idx];
    }

protected:
    // ── StatComponentBase 저장 접근 구현 ──
    double getRaw(EStat stat) const override
    {
        if (stat == EStat::None)
            return 0.0;
        return m_stats[static_cast<size_t>(stat)];
    }

    void setRaw(EStat stat, double value) override
    {
        m_stats[static_cast<size_t>(stat)] = value;
    }

    void setTotal(EStatGroup group, double value) override
    {
        // 그룹의 Total 스탯 인덱스에 저장. (총합도 같은 배열의 한 칸)
        const StatGroupInfo* pInfo = GameDataTable_Stat::GetGroupInfo(group);
        if (pInfo == nullptr || pInfo->total == EStat::None)
            return;
        m_stats[static_cast<size_t>(pInfo->total)] = value;
    }

    // 조밀 배열 전체를 순회. raw 와 총합이 같은 배열에 있으므로 한 번 순회로 둘 다 나온다.
    // None(0) 은 건너뛰고 EStat 1..Max-1 만 돌다. 0 필터는 베이스가 적용.
    void forEachStoredStat(const std::function<void(EStat, double)>& callback) const override
    {
        for (size_t i = 1; i < static_cast<size_t>(EStat::Max); ++i)
            callback(static_cast<EStat>(i), m_stats[i]);
    }

private:
    // raw + 총합 통합 배열. 인덱스 = EStat 값. 초기 전부 0.
    double m_stats[static_cast<size_t>(EStat::Max)] = {};
};
