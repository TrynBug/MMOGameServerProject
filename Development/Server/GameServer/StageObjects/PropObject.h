#pragma once

#include "pch.h"
#include "StageObjects/StageObject.h"
#include "Generated/GameData_Prop.h"

#include <memory>

// ─────────────────────────────────────────────────────────────
// PropObject
// ─────────────────────────────────────────────────────────────
//
// Stage 위의 상태를 갖는 상호작용 prop(문/레버/스위치/포탈 등). StageObject 직접 상속
// (전투/HP 가 없으므로 ActorObject 가 아니다. HP 기반 파괴 prop 은 v2 에서 ActorObject 파생.)
//
// ── 상태(state) vs 효과(effect) 분리 ──
//   상태 : 데이터(GameData_Prop.StateMode)에 의해 서버 권위로 전이된다(Toggle/OneShot/None).
//          상태 변경은 PropStateNtf 로 AOI 에 동기화된다.
//   효과 : 몬스터 스폰·텔레포트 등 게임플레이는 스크립트(OnObjectInteract) 또는
//          선언형 Behavior(Portal)가 담당한다. PropObject 는 효과를 모른다.
//
// ── 식별 ──
//   ObjectId        : 서버 발급 런타임 식별자(네트워크/타게팅용).
//   PlacementKey    : 유니티 배치 시 작성자가 붙인 인스턴스 키(layout key). 스크립트 분기용 안정 식별자.
//   PropDataKey     : GameData_Prop.Key (= 종류/type). 클라 프리팹 조회용.
//
// 가시성: 정적이라 위치 SnapshotNtf 스트림에는 포함되지 않는다(상태 변경 이벤트만 전송).
//         spawn/despawn 은 ObjectVisibilityNtf.prop_spawns 로 AOI 통보된다.
//
// 스레드: 소속 Stage 의 컨텐츠 스레드 전용(락 없음).
class PropObject : public StageObject
{
public:
    PropObject() = default;
    ~PropObject() override = default;

    PropObject(const PropObject&) = delete;
    PropObject& operator=(const PropObject&) = delete;

    // objectId 는 외부(인스턴스화 로직)에서 발급해 전달한다.
    // pPropData      : 종류 데이터(nullptr 이면 false).
    // placementKey   : 배치 인스턴스 키(layout key, 0 = 동적 스폰 등 무관).
    // initialStateOverride : 배치별 시작상태. 음수면 데이터 InitialState 사용.
    // interactRange  : 상호작용 사거리(>0 이면 사용, 아니면 데이터 InteractRange).
    // 위치/회전은 초기화 후 SetPos/SetYaw 로 설정한다.
    [[nodiscard]] bool Initialize(int64 objectId, const GameData_Prop* pPropData,
                                  int32 placementKey, int32 initialStateOverride, float interactRange);

public:
    const GameData_Prop* GetPropData()    const { return m_pPropData; }
    int32 GetPropDataKey()                const { return m_pPropData ? m_pPropData->Key : 0; }   // = 종류/type
    int32 GetPlacementKey()               const { return m_placementKey; }
    int32 GetState()                      const { return m_state; }
    float GetInteractRange()              const { return m_interactRange; }
    bool  IsInteractable()                const { return m_pPropData && m_pPropData->Interactable && !m_despawnScheduled; }

    // 선언형 동작(Portal 등) 조회 — Stage 가 효과를 트리거할 때 사용.
    EPropBehavior GetBehavior()           const { return m_pPropData ? m_pPropData->Behavior : EPropBehavior::None; }

    // ── 상호작용 ──────────────────────────────────────────────
    // 상호작용 1회 시도. 게이팅(Interactable / MaxInteract / 쿨다운) 통과 시 accepted=true 이며
    // StateMode 에 따라 상태를 전이한다. nowMs 는 Stage 단조시계(쿨다운 판정 기준).
    struct InteractResult
    {
        bool  accepted     = false;   // 게이팅 통과(상호작용 허용). false 면 무시(거짓보고/한도초과/쿨다운).
        bool  stateChanged = false;   // 상태가 실제로 바뀜 → PropStateNtf 송신 필요.
        int32 newState     = 0;       // 전이 후 상태(= GetState()).
    };
    InteractResult TryInteract(int64 nowMs);

    // 상태를 직접 설정(스크립트 SetPropState). 게이팅 없이 적용. 값이 바뀌면 true(→ Stage 가 broadcast).
    bool SetState(int32 newState);

    // ── 디스폰 타이머(DespawnDelayMs) ─────────────────────────
    // OneShot 최종전이(또는 SetState 로 디스폰 예약) 후 deltaMs 를 누적하고, DespawnDelayMs 를
    // 넘으면 true(=제거 타이밍). Stage::updateProps 가 예약된 prop 에게만 매 tick 호출한다.
    bool IsDespawnScheduled() const { return m_despawnScheduled; }
    bool AdvanceDespawnTimer(int64 deltaMs);

private:
    // 프랍 데이터 (소유권 없음, 로드 후 불변).
    const GameData_Prop* m_pPropData = nullptr;

    int32 m_placementKey = 0;    // 배치 인스턴스 키(스크립트 분기 식별자).
    int32 m_state        = 0;    // 현재 상태값.
    int32 m_interactCount= 0;    // 누적 상호작용 횟수(MaxInteract 게이팅).
    int64 m_cooldownUntilMs = 0; // 이 시각(Stage clock) 전에는 재상호작용 거부(0 = 쿨다운 없음).
    float m_interactRange   = 2.0f;

    // DespawnDelayMs 기반 자동 제거 상태.
    bool  m_despawnScheduled = false;
    int64 m_despawnElapsedMs = 0;
};

using PropObjectPtr  = std::shared_ptr<PropObject>;
using PropObjectWPtr = std::weak_ptr<PropObject>;
