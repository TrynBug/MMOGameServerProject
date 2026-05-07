#pragma once

#include "pch.h"

namespace serverbase
{

// 오브젝트 ID 생성기
// ID의 비트구조: [0(1bit)] [Timestamp(43bit)] [ServerID(10bit)] [Sequence(10bit)]
// - Timestamp : Unix 시간(ms) 하위 43bit
// - ServerID  : 1~999 (10bit = 최대 1023)
// - Sequence  : 같은 ms 내에서 최대 1023개
class ObjectIdGenerator
{
public:
    // 초기화
    void Initialize(int32 serverId);

	// ID 생성
    int64 Generate();

private:
    static constexpr int64 k_timestampMask  = 0x7FFFFFFFFFFLL; // 43bit
    static constexpr int32 k_timestampShift = 20;
    static constexpr int64 k_serverIdMask   = 0x3FF; // 10bit
    static constexpr int32 k_serverIdShift  = 10;
    static constexpr int64 k_sequenceMask   = 0x3FF; // 10bit
    static constexpr int64 k_maxSequence    = 0x3FF; // 1023

    std::mutex m_mutex;
    int64 m_lastMs = 0;
    int64 m_sequence = 0;
    int32 m_serverId = 0;
    bool m_bInitialized = false;
};

} // namespace serverbase
