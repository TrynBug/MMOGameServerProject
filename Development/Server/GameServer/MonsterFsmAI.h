#pragma once

#include "pch.h"
#include "IMonsterAI.h"

// Monster 의 완전타입은 MonsterFsmAI.cpp 에서만 필요 (헤더는 Monster& 선언만 사용).
// 전방선언은 IMonsterAI.h 가 이미 제공한다.

// ─────────────────────────────────────────────────────────────
// EMonsterState : FSM 두뇌의 내부 상태 (FSM 전용)
// ─────────────────────────────────────────────────────────────
//   Idle    : 대기. 주변에서 어그로 타겟을 탐색.
//   Chase   : 타겟 추격(근접) / 사거리 유지 이동(원거리, 카이팅).
//   Attack  : 사거리 안에서 사용할 스킬을 선택 → Monster::TryBeginCast 로 시전 시작.
//             윈드업/발동/회복 잠금은 Monster(몸체)가 처리하므로 FSM 에 Casting 상태가 없다.
//   Return  : 리쉬 초과/타겟 소실 시 스폰지점으로 복귀.
//   Dead    : 사망. 실제 디스폰/리스폰은 Stage 가 처리.
enum class EMonsterState
{
    Idle,
    Chase,
    Attack,
    Return,
    Dead,
};

// ─────────────────────────────────────────────────────────────
// MonsterFsmAI : 평면 FSM 기반 몬스터 두뇌
// ─────────────────────────────────────────────────────────────
//
// IMonsterAI 구현체. FSM 상태(EMonsterState)와 전이 로직만 보유하고,
// 실제 행동(이동/타겟/스킬/사거리)은 전부 Monster 의 공유 행동 API 를 호출한다.
// 덕분에 동일한 Monster 위에서 다른 두뇌(BT 등)로 교체할 수 있다.
//
// 근접/원거리는 별도 클래스가 아니라 Monster 의 설정값(IsRanged/GetDesiredRange 등)으로 구분한다.
class MonsterFsmAI : public IMonsterAI
{
public:
    // 매 tick 호출. 현재 FSM 상태를 처리한다 (Monster 의 공유 행동 API 사용).
    void Update(Monster& monster, int64 deltaMs) override;

    EMonsterState GetState() const { return m_state; }

private:
    // ── 상태별 처리 ──
    void updateIdle(Monster& monster, int64 deltaMs);
    void updateChase(Monster& monster, int64 deltaMs);
    void updateAttack(Monster& monster, int64 deltaMs);
    void updateReturn(Monster& monster, int64 deltaMs);
    void enterDead(Monster& monster);

private:
    EMonsterState m_state = EMonsterState::Idle;
};
