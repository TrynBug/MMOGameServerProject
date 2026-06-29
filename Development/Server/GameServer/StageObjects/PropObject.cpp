#include "pch.h"
#include "StageObjects/PropObject.h"

bool PropObject::Initialize(int64 objectId, const GameData_Prop* pPropData,
                            int32 placementKey, int32 initialStateOverride, float interactRange)
{
    if (pPropData == nullptr)
    {
        LOG_WRITE(LogLevel::Error, std::format("PropObject Initialize: null prop data. objectId={} placementKey={}", objectId, placementKey));
        return false;
    }

    // 베이스 초기화(objectId/objectType 검증 포함).
    if (!StageObject::Initialize(objectId, EObjectType::Prop))
        return false;

    m_pPropData    = pPropData;
    m_placementKey = placementKey;

    // 시작상태: 배치 override(음수면 데이터 InitialState). 음수 방지.
    int32 initState = (initialStateOverride >= 0) ? initialStateOverride : pPropData->InitialState;
    m_state = (initState >= 0) ? initState : 0;

    // 사거리: 배치 override(>0)면 사용, 아니면 데이터 기본값.
    m_interactRange = (interactRange > 0.0f) ? interactRange : pPropData->InteractRange;

    m_interactCount    = 0;
    m_cooldownUntilMs  = 0;
    m_despawnScheduled = false;
    m_despawnElapsedMs = 0;

    return true;
}

PropObject::InteractResult PropObject::TryInteract(int64 nowMs)
{
    InteractResult result;

    // ── 게이팅 ──────────────────────────────────────────────
    if (!IsInteractable())                                   // 데이터 미설정 / Interactable=false / 디스폰 예약됨
        return result;

    if (m_pPropData->MaxInteract > 0 && m_interactCount >= m_pPropData->MaxInteract)
        return result;                                       // 상호작용 횟수 한도 초과

    if (m_pPropData->CooldownMs > 0 && nowMs < m_cooldownUntilMs)
        return result;                                       // 쿨다운 중

    // ── 수락 ────────────────────────────────────────────────
    result.accepted = true;
    ++m_interactCount;
    if (m_pPropData->CooldownMs > 0)
        m_cooldownUntilMs = nowMs + m_pPropData->CooldownMs;

    // ── 상태 전이(StateMode) ────────────────────────────────
    switch (m_pPropData->StateMode)
    {
    case EPropStateMode::Toggle:
    {
        const int32 count = (m_pPropData->StateCount > 0) ? m_pPropData->StateCount : 2;
        const int32 next  = (m_state + 1) % count;
        if (next != m_state)
        {
            m_state = next;
            result.stateChanged = true;
        }
        break;
    }
    case EPropStateMode::OneShot:
    {
        if (m_state == 0)
        {
            m_state = 1;
            result.stateChanged = true;
            // DespawnDelayMs 설정 시 최종전이 후 자동 제거 예약(파괴 prop 근사).
            if (m_pPropData->DespawnDelayMs > 0)
                m_despawnScheduled = true;
        }
        break;
    }
    case EPropStateMode::None:
    default:
        // 상태 불변(예: 스위치 — 상호작용은 수락되지만 시각 상태는 그대로). 효과는 스크립트가 처리.
        break;
    }

    result.newState = m_state;
    return result;
}

bool PropObject::SetState(int32 newState)
{
    if (newState < 0)
        newState = 0;
    if (newState == m_state)
        return false;
    m_state = newState;
    return true;
}

bool PropObject::AdvanceDespawnTimer(int64 deltaMs)
{
    if (!m_despawnScheduled || m_pPropData == nullptr)
        return false;
    m_despawnElapsedMs += deltaMs;
    return m_despawnElapsedMs >= m_pPropData->DespawnDelayMs;
}
