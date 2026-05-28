#pragma once

#include "pch.h"

#include "Enum/GameEnum_Stat.h"          // EStat, EStatGroup, EStatOp
#include "Generated/GameData_Stat.h"     // GameDataTable_Stat (역인덱스 조회)

// ─────────────────────────────────────────────────────────────
// CharacterStatComponent
// ─────────────────────────────────────────────────────────────
//
// 캐릭터(Character)가 멤버로 소유하는 스탯 컴포넌트. (몬스터는 별도 경량 컴포넌트 사용)
//
// ── 저장 구조 ──
// m_stats[EStat::Max] 단일 배열에 raw 누적값과 총합값을 모두 저장한다. (인덱스 = EStat 값)
//   - raw 누적값: 힘+, 힘+%, 힘증폭, 힘감폭 등. 소스(기본/레벨/아이템/마스터리/버프)가 누적.
//   - 총합값:     힘총합 등. raw 가 갱신될 때마다 즉시 재계산되는 캐시. 핫패스는 이것을 읽는다.
//
// ── 누적 모델 (모델 B) ──
// 소스가 붙을 때 ApplyStat, 빠질 때 RemoveStat 을 호출한다. in-place 누적이며 출처를 추적하지 않는다.
// (소스 객체 — 아이템/버프 — 가 자기 기여분을 알고 있어서, 뺄 때 같은 값으로 RemoveStat 한다.)
// 곱연산/점감곱연산은 역연산으로 제거하므로 부동소수점 오차가 누적되지만, 무시할 수준으로 본다.
// 곱연산 누적값이 ×0 미만으로 가면 역연산 분모가 0/음수가 되므로,
// Amp/Reduce raw 값은 갱신 직후 Stat 게임데이터의 MinApplyValue 로 clamp 하여 이를 방지한다.
//
// ── 총합 합성 공식 ──
// 기본힘은 별도 항이 아니라 Add 슬롯에 누적되어 있다. (캐릭터 생성 시 ApplyStat(StrAdd, 기본힘))
//   total = m_stats[Add] * (1 + m_stats[AddPct]*0.01) * (1 + m_stats[Amp]*0.01) * (1 - m_stats[Reduce]*0.01)
// 합성 후 Total 의 Min/MaxApplyValue 로 clamp 하여 m_stats[Total] 에 저장한다.
// 슬롯이 데이터에 없으면(EStat::None) 그 raw 값은 0 으로 간주 → 곱셈 항이 1 이 되어 무영향.
//
// ── 스레드 ──
// 소속 Stage 의 컨텐츠 스레드에서만 접근한다. 별도 락 없음.
class CharacterStatComponent
{
public:
    CharacterStatComponent() = default;

    CharacterStatComponent(const CharacterStatComponent&) = delete;
    CharacterStatComponent& operator=(const CharacterStatComponent&) = delete;

public:
    // ── 핫패스: 최종값 읽기 (O(1)) ──
    // 보통 총합 스탯을 읽는다. 예: Get(EStat::StrTotal).
    // raw 스탯도 읽을 수 있다. 유효하지 않은 stat 이면 0.
    double Get(EStat stat) const
    {
        const size_t idx = static_cast<size_t>(stat);
        if (idx >= static_cast<size_t>(EStat::Max))
            return 0.0;
        return m_stats[idx];
    }

    // ── 스탯 적용 / 해제 ──
    // 소스(아이템/버프/기본/레벨/마스터리)가 자기 기여분을 들고 호출한다.
    // raw 누적값을 Op 공식으로 갱신한 뒤, 소속 그룹의 총합을 즉시 재계산한다.
    // stat 은 raw 스탯이어야 한다. (Total 스탯을 직접 Apply/Remove 하지 말 것)
    void ApplyStat(EStat stat, double value);
    void RemoveStat(EStat stat, double value);

private:
    // 한 그룹의 총합을 raw 들로부터 재계산하여 m_stats[Total] 에 저장한다.
    void recomputeTotal(EStatGroup group);

    // Op 별 raw 누적 공식. bAdd=true 면 더하기, false 면 빼기(역연산).
    static double applyOp(EStatOp op, double cur, double value, bool bAdd);

    // 슬롯 stat 의 현재 raw 값. stat==None 이면 0 (슬롯 없음 → 무영향).
    double rawOrZero(EStat stat) const
    {
        if (stat == EStat::None)
            return 0.0;
        return m_stats[static_cast<size_t>(stat)];
    }

private:
    // raw + 총합 통합 배열. 인덱스 = EStat 값. 초기 전부 0.
    double m_stats[static_cast<size_t>(EStat::Max)] = {};
};
