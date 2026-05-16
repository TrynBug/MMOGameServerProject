#include "pch.h"
#include "Stage.h"

namespace
{
    constexpr int64 k_heartbeatIntervalMs = 5000;   // 5초마다 1번 heartbeat 로그
}

Stage::Stage(int64 stageId, StageType stageType)
    : m_stageId(stageId)
    , m_stageType(stageType)
{
}

void Stage::OnStart()
{
    LOG_WRITE(LogLevel::Info, std::format("Stage::OnStart - stageId={} stageType={}",
        m_stageId, static_cast<int>(m_stageType)));
}

void Stage::OnUpdate(int64 deltaMs)
{
    // heartbeat 로그 (5초마다 1번)
    m_heartbeatAccumMs += deltaMs;
    if (m_heartbeatAccumMs >= k_heartbeatIntervalMs)
    {
        m_heartbeatAccumMs = 0;
        LOG_WRITE(LogLevel::Debug, std::format("Stage heartbeat. stageId={} stageType={}",
            m_stageId, static_cast<int>(m_stageType)));
    }

    // 파생 클래스 로직
    OnStageUpdate(deltaMs);
}

void Stage::OnStop()
{
    LOG_WRITE(LogLevel::Info, std::format("Stage::OnStop - stageId={} stageType={}",
        m_stageId, static_cast<int>(m_stageType)));
}
