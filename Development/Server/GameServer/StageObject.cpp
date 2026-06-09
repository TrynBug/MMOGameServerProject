#include "pch.h"
#include "StageObject.h"

bool StageObject::Initialize(int64 objectId, EObjectType objectType)
{
    if (objectId == 0 || objectType == EObjectType::None)
    {
        LOG_WRITE(LogLevel::Error, std::format("invalid args. objectId={} objectType={}", objectId, static_cast<int>(objectType)));
        return false;
    }

    m_objectId   = objectId;
    m_objectType = objectType;
    return true;
}

void StageObject::SetUpdateIntervalMs(int64 intervalMs)
{
    if (intervalMs < k_updateTickUnitMs)
        intervalMs = k_updateTickUnitMs;
    // 50ms 의 배수로 내림 정렬 (스케줄러가 tick 경계에서만 발화하므로).
    m_updateIntervalMs = (intervalMs / k_updateTickUnitMs) * k_updateTickUnitMs;
}

bool StageObject::AdvanceUpdateClock(int64 deltaMs, int64& outElapsedMs)
{
    m_updateAccumMs += deltaMs;
    if (m_updateAccumMs < m_updateIntervalMs)
        return false;
    outElapsedMs    = m_updateAccumMs;
    m_updateAccumMs = 0;
    return true;
}
