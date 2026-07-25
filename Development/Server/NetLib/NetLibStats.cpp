#include "pch.h"
#include "NetLibStats.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <memory>
#include <mutex>
#include <vector>

namespace netlib
{

constexpr size_t StatCounterEnd = static_cast<size_t>(StatCounter::_End);
constexpr size_t StatGaugeEnd = static_cast<size_t>(StatGauge::_End);

// 카테고리 이름
// StatCounter enum 순서와 정확히 일치해야 함.
const char* s_counterNames[StatCounterEnd] = {
    "AcceptCount",
    "SessionCreated",
    "SessionDestroyed",
    "GracefulDisconnect",
    "AbnormalDisconnect",
    "InvalidPacketHeader",
    "PacketPoolAllocFail",
    "RecvPosted",
    "RecvCompleted",
    "RecvKnownFailed",
    "RecvUnknownFailed",
    "RecvBufferFull",
    "SendPosted",
    "SendCompleted",
    "SendKnownFailed",
    "SendUnknownFailed",
    "ConnectPosted",
    "ConnectCompleted",
    "ConnectFailed",
    "RecvBytes",
    "RecvPackets",
    "SendBytes",
    "SendPackets",
};
static_assert(sizeof(s_counterNames) / sizeof(s_counterNames[0]) == StatCounterEnd, "s_counterNames must match StatCounter enum size");

const char* s_gaugeNames[StatGaugeEnd] = {
    "ActiveSessions",
    "RecvBufferUsedBytes",
    "SendQueuePackets",
    "SendQueueBytes",
    "SendInFlightPackets",
    "SendInFlightBytes",
};
static_assert(sizeof(s_gaugeNames) / sizeof(s_gaugeNames[0]) == StatGaugeEnd, "s_gaugeNames must match StatGauge enum size");

// TLS 카운터. 각 스레드마다 하나씩 존재한다.
// Inc는 현재 스레드만 수행하지만 통계 수집 스레드가 동시에 읽으므로 atomic이 필요하다.
struct alignas(64) PerThreadCounters  // cache line 정렬로 false sharing 방지
{
    std::array<std::atomic<uint64>, StatCounterEnd> values{};
    std::array<std::atomic<int64>, StatGaugeEnd> gauges{};
    // alignas(64)에 의해 뒤쪽은 자동으로 패딩됨
};

// 모든 PerThreadCounters를 관리하는 싱글톤 클래스
class CounterRegistry
{
public:
    static CounterRegistry& Instance()
    {
        static CounterRegistry sInstance;
        return sInstance;
    }

    // 현재 스레드의 TLS카운터를 생성하고 등록한다.
    // 반환된 포인터는 이 스레드의 TLS guard가 소유
    PerThreadCounters* Register()
    {
        auto upCounter = std::make_unique<PerThreadCounters>();
        PerThreadCounters* ptr = upCounter.get();

        std::lock_guard<std::mutex> lock(m_mutex);
        m_alive.push_back(std::move(upCounter));

        return ptr;
    }

    // 스레드가 종료될 때 호출. raw가 가리키는 카운터 값들을 orphan에 더한 다음 alive 벡터에서 해당 unique_ptr 제거
    void Unregister(PerThreadCounters* pCounter)
    {
        if (pCounter == nullptr)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(m_mutex);

        // 값을 orphan에 누적
        for (size_t i = 0; i < StatCounterEnd; ++i)
        {
            m_orphanTotals[i] += pCounter->values[i].load(std::memory_order_relaxed);
        }
        for (size_t i = 0; i < StatGaugeEnd; ++i)
        {
            m_orphanGauges[i] += pCounter->gauges[i].load(std::memory_order_relaxed);
        }

        // alive에서 제거
        for (auto iter = m_alive.begin(); iter != m_alive.end(); ++iter)
        {
            if (iter->get() == pCounter)
            {
                m_alive.erase(iter);
                break;
            }
        }
    }

    int64 Sum(StatGauge gauge) const
    {
        const size_t idx = static_cast<size_t>(gauge);
        if (idx >= StatGaugeEnd)
            return 0;

        std::lock_guard<std::mutex> lock(m_mutex);
        int64 sum = m_orphanGauges[idx];
        for (const auto& p : m_alive)
            sum += p->gauges[idx].load(std::memory_order_relaxed);
        return sum;
    }

    // 특정 카운터의 총합 반환
    uint64 Sum(StatCounter c) const
    {
        const size_t idx = static_cast<size_t>(c);
        if (idx >= StatCounterEnd)
        {
            return 0;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        uint64 sum = m_orphanTotals[idx];
        for (const auto& p : m_alive)
        {
            sum += p->values[idx].load(std::memory_order_relaxed);
        }
        return sum;
    }

    void SumAll(std::array<int64, StatGaugeEnd>& out) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        out = m_orphanGauges;
        for (const auto& p : m_alive)
        {
            for (size_t i = 0; i < StatGaugeEnd; ++i)
                out[i] += p->gauges[i].load(std::memory_order_relaxed);
        }
    }

    // 모든 카운터 스냅샷
    void SumAll(std::array<uint64, StatCounterEnd>& out) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (size_t i = 0; i < StatCounterEnd; ++i)
        {
            out[i] = m_orphanTotals[i];
        }
        for (const auto& p : m_alive)
        {
            for (size_t i = 0; i < StatCounterEnd; ++i)
            {
                out[i] += p->values[i].load(std::memory_order_relaxed);
            }
        }
    }

    // 모든 카운터 0 초기화
    void ResetAll()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (size_t i = 0; i < StatCounterEnd; ++i)
        {
            m_orphanTotals[i] = 0;
        }
        for (auto& p : m_alive)
        {
            for (size_t i = 0; i < StatCounterEnd; ++i)
            {
                p->values[i].store(0, std::memory_order_relaxed);
            }
            for (size_t i = 0; i < StatGaugeEnd; ++i)
                p->gauges[i].store(0, std::memory_order_relaxed);
        }
        m_orphanGauges.fill(0);
    }

private:
    CounterRegistry() = default;

    mutable std::mutex                              m_mutex;
    std::vector<std::unique_ptr<PerThreadCounters>> m_alive;
    std::array<uint64, StatCounterEnd>               m_orphanTotals{};
    std::array<int64, StatGaugeEnd>                   m_orphanGauges{};
};

// TLS 카운터 포인터 관리
// TLS 변수 자체는 POD이어야 안전하므로 raw pointer로 두고, TlsGuard의 소멸자에서 registry에 unregister
// 주의: 메인 스레드에서 Inc를 호출하지 않는 한 TlsGuard는 활성화되지 않는다. 워커 스레드가 생성되고 Inc를 첫 호출하는 시점에 init
struct TlsGuard
{
    PerThreadCounters* counters = nullptr;

    TlsGuard() = default;
    ~TlsGuard()
    {
        if (counters != nullptr)
        {
            CounterRegistry::Instance().Unregister(counters);
            counters = nullptr;
        }
    }
};

thread_local TlsGuard tl_guard;

// 현재 스레드의 카운터 배열. 없으면 생성.
inline PerThreadCounters* GetOrInitTlsCounters()
{
    if (tl_guard.counters == nullptr)
    {
        tl_guard.counters = CounterRegistry::Instance().Register();
    }
    return tl_guard.counters;
}


// 카운터를 delta만큼 증가한다.
void NetLibStats::Inc(StatCounter counter, uint64 delta)
{
    const size_t idx = static_cast<size_t>(counter);
    if (idx >= StatCounterEnd)
    {
        return;
    }

    // 현재 스레드의 TlsCounter 얻기
    PerThreadCounters* pCounter = GetOrInitTlsCounters();

    // 이 카운터를 갱신하는 스레드는 하나지만 통계 수집 스레드가 동시에 읽을 수 있다.
    pCounter->values[idx].fetch_add(delta, std::memory_order_relaxed);
}

void NetLibStats::Add(StatGauge gauge, int64 delta)
{
    const size_t idx = static_cast<size_t>(gauge);
    if (idx >= StatGaugeEnd || delta == 0)
        return;

    GetOrInitTlsCounters()->gauges[idx].fetch_add(delta, std::memory_order_relaxed);
}

// 현재 카운터값 얻기
uint64 NetLibStats::GetCount(StatCounter counter)
{
    return CounterRegistry::Instance().Sum(counter);
}

// 모든 카운터값을 한번에 얻는다. 배열 크기는 StatCounter::_Count.
void NetLibStats::GetAllCount(std::array<uint64, StatCounterEnd>& outCounts)
{
    for (size_t i = 0; i < StatCounterEnd; ++i)
    {
        outCounts[i] = 0;
    }

    CounterRegistry::Instance().SumAll(outCounts);
}

int64 NetLibStats::GetGauge(StatGauge gauge)
{
    return CounterRegistry::Instance().Sum(gauge);
}

void NetLibStats::GetAllGauges(std::array<int64, StatGaugeEnd>& outGauges)
{
    outGauges.fill(0);
    CounterRegistry::Instance().SumAll(outGauges);
}

// Counter 이름 얻기
const char* NetLibStats::GetCounterName(StatCounter counter)
{
    const size_t idx = static_cast<size_t>(counter);
    if (idx >= StatCounterEnd)
    {
        return "Unknown";
    }
    return s_counterNames[idx];
}

const char* NetLibStats::GetGaugeName(StatGauge gauge)
{
    const size_t idx = static_cast<size_t>(gauge);
    return idx < StatGaugeEnd ? s_gaugeNames[idx] : "Unknown";
}

void NetLibStats::LogSnapshot()
{
    std::array<uint64, StatCounterEnd> counts{};
    GetAllCount(counts);

    std::printf("[NetStats]");
    for (size_t i = 0; i < StatCounterEnd; ++i)
    {
        std::printf(" %s=%llu", s_counterNames[i], static_cast<unsigned long long>(counts[i]));
    }
    std::array<int64, StatGaugeEnd> gauges{};
    GetAllGauges(gauges);
    for (size_t i = 0; i < StatGaugeEnd; ++i)
        std::printf(" %s=%lld", s_gaugeNames[i], static_cast<long long>(gauges[i]));
    std::printf("\n");
}

void NetLibStats::ResetAll()
{
    CounterRegistry::Instance().ResetAll();
}

} // namespace netlib
