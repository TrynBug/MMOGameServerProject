#pragma once

#include "pch.h"

#include "Enum/GameEnum_Stat.h"          // EStat, EStatGroup, EStatOp
#include "Generated/GameData_Stat.h"     // GameDataTable_Stat (역인덱스 조회)

#include <functional>

// ─────────────────────────────────────────────────────────────
// StatComponentBase
// ─────────────────────────────────────────────────────────────
//
// 스탯 컴포넌트의 공통 베이스. "스탯 누적/합성 공식" 과 "갱신 흐름" 을 담당한다.
// 실제 저장 방식(조밀 배열 vs 희소 맵)은 파생 클래스가 가상함수로 제공한다.
//   - CharacterStatComponent : raw+총합을 double[EStat::Max] 조밀 배열에 보관
//   - BasicStatComponent     : raw 는 unordered_map(희소), 총합은 double[EStatGroup::Max]
//
// ── 누적 모델 (모델 B) ──
// 소스(기본/레벨/아이템/마스터리/버프)가 붙을 때 ApplyStat, 빠질 때 RemoveStat 을 호출한다.
// in-place 누적이며 출처를 추적하지 않는다. (소스 객체가 자기 기여분을 알고 있어 같은 값으로 되돌린다.)
// 곱연산/점감곱연산은 역연산으로 제거하므로 부동소수점 오차가 누적되지만 무시할 수준으로 본다.
// 곱연산 누적값이 ×0 미만으로 가면 역연산 분모가 무너지므로, Amp/Reduce raw 는 갱신 직후
// Stat 게임데이터의 MinApplyValue 로 clamp 한다.
//
// ── 총합 합성 공식 ──
// 기본값(예: 기본힘)은 별도 항이 아니라 Add 슬롯에 누적되어 있다.
//   total = Add * (1 + AddPct*0.01) * (1 + Amp*0.01) * (1 - Reduce*0.01)
// 합성 후 Total 스탯의 Min/MaxApplyValue 로 clamp 한다.
// 슬롯이 데이터에 없으면(EStat::None) 그 raw 값은 0 으로 간주 → 곱셈 항이 1 이 되어 무영향.
//
// ── 스레드 ──
// 소속 Stage 의 컨텐츠 스레드에서만 접근한다. 별도 락 없음.
//
// ── 핫패스 ──
// 총합 읽기(이동/대미지 계산)는 이 클래스가 아니라 ActorObject::GetStatTotal() 을 통한다.
// 이 클래스의 가상함수(getRaw 등)는 ApplyStat/RemoveStat(=버프 시점)에서만 호출되며 핫패스가 아니다.
class StatComponentBase
{
public:
    StatComponentBase() = default;
    virtual ~StatComponentBase() = default;

    StatComponentBase(const StatComponentBase&) = delete;
    StatComponentBase& operator=(const StatComponentBase&) = delete;

public:
    void ApplyStat(EStat stat, double value);
    void RemoveStat(EStat stat, double value);

    // ── 0 이 아닌 스탯 순회 ──
    // 저장된 스탯 중 값이 0(부동소수점 오차 포함)이 아닌 항목만 콜백한다.
    // 클라로 보낼 StatUpdateNtf 스냅샷을 만들 때 사용한다(0인 스탯은 보내지 않는 규약).
    // 핫패스가 아니다(장비/버프/입장 시점에만 호출). 패킷 타입은 모르고 (EStat, double) 만 넘긴다.
    void ForEachNonZeroStat(const std::function<void(EStat, double)>& callback) const;

protected:
    // ── 파생이 제공하는 저장 접근 (핫패스 아님: 갱신/합성 흐름에서만 호출) ──
    virtual double getRaw(EStat stat) const = 0;             // 없으면 0 리턴
    virtual void   setRaw(EStat stat, double value) = 0;
    virtual void   setTotal(EStatGroup group, double value) = 0;

    // 저장된 모든 스탯(raw + 총합)을 순회하며 콜백한다. 0 필터는 베이스가 ForEachNonZeroStat 에서 적용하므로
    // 파생은 자신의 저장소에 들어있는 값을 그대로 넘기면 된다(0 포함 가능).
    //   - Character : double[EStat::Max] 배열 전체 (raw+총합이 한 배열)
    //   - Basic     : raw 맵 + 총합 배열 (둘이 분리 저장)
    virtual void   forEachStoredStat(const std::function<void(EStat, double)>& callback) const = 0;

    // ── 공통 공식 / 흐름 (베이스 구현) ──
    // Op 별 raw 누적 공식. bAdd=true 면 더하기, false 면 빼기(역연산).
    static double applyOp(EStatOp op, double cur, double value, bool bAdd);

    // 한 그룹의 총합을 raw 들로부터 재계산하여 setTotal 으로 저장한다.
    void recomputeTotal(EStatGroup group);

private:
    // ApplyStat/RemoveStat 공통 본체. bAdd 로 더하기/빼기 구분.
    void changeStat(EStat stat, double value, bool bAdd);
};
