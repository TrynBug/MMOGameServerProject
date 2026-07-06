#pragma once

#include "pch.h"

// 전방선언: 인터페이스는 Monster 의 완전타입이 필요 없다 (구현부에서만 필요).
class Monster;

// ─────────────────────────────────────────────────────────────
// IMonsterAI : 몬스터 두뇌(의사결정) 인터페이스
// ─────────────────────────────────────────────────────────────
//
// 몬스터의 "행동 주체"(Monster)와 "의사결정"(AI)을 분리하기 위한 인터페이스.
// Monster 는 unique_ptr<IMonsterAI> 로 두뇌를 소유하며, 매 tick Update 를 위임한다.
//
// 구현체는 Monster 의 공유 행동 API(이동/타겟 탐색/스킬 선택·시전/사거리 조회 등)만
// 호출하여 행동을 결정한다. 따라서 같은 Monster 위에서 FSM/BT/Utility 등 어떤 두뇌든
// 교체 가능하다 (Monster::SetAI).
//
// 스레드: Monster::Update 가 컨텐츠 스레드에서 호출하므로 구현체도 컨텐츠 스레드 전용이다.
class IMonsterAI
{
public:
    virtual ~IMonsterAI() = default;

    // 매 tick 호출. monster 의 공유 행동 API 를 사용해 1 tick 의사결정/행동을 수행한다.
    virtual void Update(Monster& monster, int64 deltaMs) = 0;

    // 피격 등으로 "도발"됐을 때 호출(이벤트). 두뇌가 즉시 교전 상태로 반응하도록 한다.
    // 몸체(Monster::OnDamagedBy)가 이미 타겟을 세팅한 뒤 호출하므로, 여기선 상태 전이만 하면 된다.
    // 기본 무동작 — 대응할 필요 없는 두뇌(단순몹 등)는 오버라이드하지 않아도 된다.
    virtual void OnProvoked(Monster& monster) { (void)monster; }
};
