#include "pch.h"
#include "ObjectIdGenerator.h"

namespace serverbase
{

void ObjectIdGenerator::Initialize(int32 serverId)
{
    if (m_bInitialized)
    {
        LOG_WRITE(LogLevel::Error, "ObjectIdGenerator::Initialize - already initialized. ignoring. current=" + std::to_string(m_serverId) + " requested=" + std::to_string(serverId));
        return;
    }
    m_serverId = serverId;
    m_bInitialized = true;
}

int64 ObjectIdGenerator::Generate()
{
    using namespace std::chrono;
    int64 nowMs = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();

    int64 seq = 0;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (nowMs <= m_lastMs)
        {
            // 같은 ms이거나 시스템시간이 과거인 경우: sequence 증가
            nowMs = m_lastMs;
            seq = ++m_sequence;
            if (seq > k_maxSequence)
            {
                // sequence 소진: 다음 ms로 강제 이동
                ++m_lastMs;
                nowMs = m_lastMs;
                m_sequence = 0;
                seq = 0;
            }
        }
        else
        {
            m_lastMs = nowMs;
            m_sequence = 0;
            seq = 0;
        }
    }

    // [0(1)] [Timestamp(43)] [ServerID(10)] [Sequence(10)]
    int64 id = ((nowMs & k_timestampMask) << k_timestampShift)
        | ((static_cast<int64>(m_serverId) & k_serverIdMask) << k_serverIdShift)
        | (seq & k_sequenceMask);

    return id;
}

}