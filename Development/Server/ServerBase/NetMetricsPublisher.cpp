#include "pch.h"
#include "NetMetricsPublisher.h"

#include "IoContext.h"

#include <algorithm>

namespace serverbase
{
namespace
{
// PacketPool::GetStats()는 모든 bucket의 shard lock을 잠깐씩 획득하므로 매 scrape마다 호출하지 않는다.
constexpr auto PacketPoolSnapshotInterval = std::chrono::seconds(30);
}

bool NetMetricsPublisher::Initialize(MetricsRegistry& registry, netlib::IoContext& ioContext)
{
    // IoContext와 PacketPool 구성이 모두 끝난 뒤 호출되므로 capacity bucket 목록은 이후 고정되어 있다고 가정한다.
    m_pIoContext = &ioContext;
    m_pRegistry = &registry;
    return registerNetMetrics(registry) && registerPacketPoolMetrics(registry);
}

bool NetMetricsPublisher::registerNetMetrics(MetricsRegistry& registry)
{
    // NetLib Stat enum 값과 의미가 대응되는 공용 metric enum을 각각 명시적으로 등록한다.
    // connection/session lifecycle과 byte/packet 처리량은 process 수명 누적 Counter로 등록한다.
    // disconnect, recv/send error처럼 같은 원인의 세부 분류는 제한된 reason label로 하나의 family에 묶는다.
    bool registered = true;
    registered &= registry.AddCounter(CounterMetric::Net_AcceptedConnections, "mmo_net_accept_total", "Accepted TCP connections.");
    registered &= registry.AddCounter(CounterMetric::Net_SessionsCreated, "mmo_net_sessions_created_total", "Created NetLib session objects.");
    registered &= registry.AddCounter(CounterMetric::Net_SessionsDestroyed, "mmo_net_sessions_destroyed_total", "Destroyed NetLib session objects.");
    registered &= registry.AddCounter(CounterMetric::Net_GracefulDisconnects, "mmo_net_disconnect_total", "TCP disconnects by reason.", { { "reason", "graceful" } });
    registered &= registry.AddCounter(CounterMetric::Net_AbnormalDisconnects, "mmo_net_disconnect_total", "TCP disconnects by reason.", { { "reason", "abnormal" } });
    registered &= registry.AddCounter(CounterMetric::Net_InvalidPacketHeaders, "mmo_net_invalid_packet_total", "Rejected packets by reason.", { { "reason", "header" } });
    registered &= registry.AddCounter(CounterMetric::Net_PacketPoolAllocationFailures, "mmo_packet_pool_alloc_fail_total", "PacketPool allocations rejected because no bucket can serve the size.");
    registered &= registry.AddCounter(CounterMetric::Net_RecvOperationsPosted, "mmo_net_recv_posted_total", "WSARecv operations posted.");
    registered &= registry.AddCounter(CounterMetric::Net_RecvOperationsCompleted, "mmo_net_recv_completed_total", "WSARecv operations completed with data.");
    registered &= registry.AddCounter(CounterMetric::Net_RecvKnownErrors, "mmo_net_recv_errors_total", "Receive errors by classification.", { { "reason", "known" } });
    registered &= registry.AddCounter(CounterMetric::Net_RecvUnknownErrors, "mmo_net_recv_errors_total", "Receive errors by classification.", { { "reason", "unknown" } });
    registered &= registry.AddCounter(CounterMetric::Net_RecvBufferFullDisconnects, "mmo_net_recv_buffer_full_total", "Disconnects caused by a full session receive buffer.");
    registered &= registry.AddCounter(CounterMetric::Net_SendOperationsPosted, "mmo_net_send_posted_total", "WSASend operations posted.");
    registered &= registry.AddCounter(CounterMetric::Net_SendOperationsCompleted, "mmo_net_send_completed_total", "WSASend operations completed.");
    registered &= registry.AddCounter(CounterMetric::Net_SendKnownErrors, "mmo_net_send_errors_total", "Send errors by classification.", { { "reason", "known" } });
    registered &= registry.AddCounter(CounterMetric::Net_SendUnknownErrors, "mmo_net_send_errors_total", "Send errors by classification.", { { "reason", "unknown" } });
    registered &= registry.AddCounter(CounterMetric::Net_ConnectOperationsPosted, "mmo_net_connect_posted_total", "ConnectEx operations posted.");
    registered &= registry.AddCounter(CounterMetric::Net_ConnectOperationsCompleted, "mmo_net_connect_completed_total", "ConnectEx operations completed.");
    registered &= registry.AddCounter(CounterMetric::Net_ConnectOperationsFailed, "mmo_net_connect_failed_total", "ConnectEx operations that failed.");
    registered &= registry.AddCounter(CounterMetric::Net_RecvBytes, "mmo_net_recv_bytes_total", "Bytes delivered by completed receive operations.");
    registered &= registry.AddCounter(CounterMetric::Net_RecvPackets, "mmo_net_recv_packets_total", "Complete application packets parsed from receive buffers.");
    registered &= registry.AddCounter(CounterMetric::Net_SendBytes, "mmo_net_send_bytes_total", "Bytes reported by completed send operations.");
    registered &= registry.AddCounter(CounterMetric::Net_SendPackets, "mmo_net_send_packets_total", "Application packets released by completed send operations.");

    // 아래 값은 현재 session/buffer/send queue 점유량이므로 증가와 감소가 가능한 Gauge다.
    // queue와 in-flight를 분리하면 application 적체인지 kernel에 이미 게시된 작업인지 구분할 수 있다.
    registered &= registry.AddGauge(GaugeMetric::Net_ActiveSessions, "mmo_net_sessions_active", "Currently connected NetLib sessions.");
    registered &= registry.AddGauge(GaugeMetric::Net_RecvBufferUsedBytes, "mmo_net_recv_buffer_used_bytes", "Bytes currently retained in all session receive buffers.");
    registered &= registry.AddGauge(GaugeMetric::Net_SendQueuePackets, "mmo_net_send_queue_packets", "Application packets waiting for WSASend.");
    registered &= registry.AddGauge(GaugeMetric::Net_SendQueueBytes, "mmo_net_send_queue_bytes", "Bytes waiting for WSASend.");
    registered &= registry.AddGauge(GaugeMetric::Net_SendInFlightPackets, "mmo_net_send_inflight_packets", "Application packets referenced by in-flight WSASend operations.");
    registered &= registry.AddGauge(GaugeMetric::Net_SendInFlightBytes, "mmo_net_send_inflight_bytes", "Bytes referenced by in-flight WSASend operations.");
    return registered;
}

bool NetMetricsPublisher::registerPacketPoolMetrics(MetricsRegistry& registry)
{
    // capacity별 series를 만들지 않고 모든 bucket의 합계와 최악 imbalance만 한 series로 게시한다.
    bool registered = true;
    registered &= registry.AddCounter(CounterMetric::PacketPool_Allocations, "mmo_packet_pool_alloc_total", "PacketPool allocations across all capacity buckets.");
    registered &= registry.AddCounter(CounterMetric::PacketPool_Frees, "mmo_packet_pool_free_total", "Packets returned across all PacketPool capacity buckets.");
    registered &= registry.AddCounter(CounterMetric::PacketPool_CreatedPackets, "mmo_packet_pool_created_total", "New Packet objects created after pool misses across all buckets.");
    registered &= registry.AddCounter(CounterMetric::PacketPool_ScanMisses, "mmo_packet_pool_scan_miss_total", "Empty PacketPool shards encountered while allocating across all buckets.");
    registered &= registry.AddGauge(GaugeMetric::PacketPool_Held, "mmo_packet_pool_held", "Free Packet objects currently held across all PacketPool buckets.");
    registered &= registry.AddGauge(GaugeMetric::PacketPool_InUse, "mmo_packet_pool_in_use", "Packet objects currently checked out across all PacketPool buckets.");
    registered &= registry.AddGauge(GaugeMetric::PacketPool_HitRatio, "mmo_packet_pool_hit_ratio", "Cumulative allocation hit ratio across all PacketPool buckets.");
    registered &= registry.AddGauge(GaugeMetric::PacketPool_ShardImbalanceRatio, "mmo_packet_pool_shard_imbalance_ratio", "Worst shard imbalance ratio among PacketPool buckets.");
    if (!registered)
        return false;

    m_nextPacketPoolSnapshot = std::chrono::steady_clock::time_point::min();
    return true;
}

void NetMetricsPublisher::Publish()
{
    // NetLibStats는 여러 IOCP thread의 relaxed atomic/TLS 통계를 합산한 snapshot이다.
    // 값은 누적 절대값으로 Set해 scrape 횟수와 관계없이 counter가 이중 증가하지 않게 한다.
    // Counter는 NetLib가 process 시작 이후 누적한 절대값이다. Registry Counter에 Inc가 아니라 Set을 사용해야
    // 매 scrape마다 같은 누적값을 재가산하는 오류가 생기지 않는다.
    std::array<uint64, static_cast<size_t>(netlib::StatCounter::_End)> counters{};
    netlib::NetLibStats::GetAllCount(counters);
    m_pRegistry->Set(CounterMetric::Net_AcceptedConnections, counters[static_cast<size_t>(netlib::StatCounter::AcceptCount)]);
    m_pRegistry->Set(CounterMetric::Net_SessionsCreated, counters[static_cast<size_t>(netlib::StatCounter::SessionCreated)]);
    m_pRegistry->Set(CounterMetric::Net_SessionsDestroyed, counters[static_cast<size_t>(netlib::StatCounter::SessionDestroyed)]);
    m_pRegistry->Set(CounterMetric::Net_GracefulDisconnects, counters[static_cast<size_t>(netlib::StatCounter::GracefulDisconnect)]);
    m_pRegistry->Set(CounterMetric::Net_AbnormalDisconnects, counters[static_cast<size_t>(netlib::StatCounter::AbnormalDisconnect)]);
    m_pRegistry->Set(CounterMetric::Net_InvalidPacketHeaders, counters[static_cast<size_t>(netlib::StatCounter::InvalidPacketHeader)]);
    m_pRegistry->Set(CounterMetric::Net_PacketPoolAllocationFailures, counters[static_cast<size_t>(netlib::StatCounter::PacketPoolAllocFail)]);
    m_pRegistry->Set(CounterMetric::Net_RecvOperationsPosted, counters[static_cast<size_t>(netlib::StatCounter::RecvPosted)]);
    m_pRegistry->Set(CounterMetric::Net_RecvOperationsCompleted, counters[static_cast<size_t>(netlib::StatCounter::RecvCompleted)]);
    m_pRegistry->Set(CounterMetric::Net_RecvKnownErrors, counters[static_cast<size_t>(netlib::StatCounter::RecvKnownFailed)]);
    m_pRegistry->Set(CounterMetric::Net_RecvUnknownErrors, counters[static_cast<size_t>(netlib::StatCounter::RecvUnknownFailed)]);
    m_pRegistry->Set(CounterMetric::Net_RecvBufferFullDisconnects, counters[static_cast<size_t>(netlib::StatCounter::RecvBufferFull)]);
    m_pRegistry->Set(CounterMetric::Net_SendOperationsPosted, counters[static_cast<size_t>(netlib::StatCounter::SendPosted)]);
    m_pRegistry->Set(CounterMetric::Net_SendOperationsCompleted, counters[static_cast<size_t>(netlib::StatCounter::SendCompleted)]);
    m_pRegistry->Set(CounterMetric::Net_SendKnownErrors, counters[static_cast<size_t>(netlib::StatCounter::SendKnownFailed)]);
    m_pRegistry->Set(CounterMetric::Net_SendUnknownErrors, counters[static_cast<size_t>(netlib::StatCounter::SendUnknownFailed)]);
    m_pRegistry->Set(CounterMetric::Net_ConnectOperationsPosted, counters[static_cast<size_t>(netlib::StatCounter::ConnectPosted)]);
    m_pRegistry->Set(CounterMetric::Net_ConnectOperationsCompleted, counters[static_cast<size_t>(netlib::StatCounter::ConnectCompleted)]);
    m_pRegistry->Set(CounterMetric::Net_ConnectOperationsFailed, counters[static_cast<size_t>(netlib::StatCounter::ConnectFailed)]);
    m_pRegistry->Set(CounterMetric::Net_RecvBytes, counters[static_cast<size_t>(netlib::StatCounter::RecvBytes)]);
    m_pRegistry->Set(CounterMetric::Net_RecvPackets, counters[static_cast<size_t>(netlib::StatCounter::RecvPackets)]);
    m_pRegistry->Set(CounterMetric::Net_SendBytes, counters[static_cast<size_t>(netlib::StatCounter::SendBytes)]);
    m_pRegistry->Set(CounterMetric::Net_SendPackets, counters[static_cast<size_t>(netlib::StatCounter::SendPackets)]);

    // Gauge는 active session, queue byte처럼 현재 상태의 합계다. TLS/per-thread 값을 수집 시 합산해 덮어쓴다.
    std::array<int64, static_cast<size_t>(netlib::StatGauge::_End)> gauges{};
    netlib::NetLibStats::GetAllGauges(gauges);
    m_pRegistry->Set(GaugeMetric::Net_ActiveSessions, static_cast<double>(gauges[static_cast<size_t>(netlib::StatGauge::ActiveSessions)]));
    m_pRegistry->Set(GaugeMetric::Net_RecvBufferUsedBytes, static_cast<double>(gauges[static_cast<size_t>(netlib::StatGauge::RecvBufferUsedBytes)]));
    m_pRegistry->Set(GaugeMetric::Net_SendQueuePackets, static_cast<double>(gauges[static_cast<size_t>(netlib::StatGauge::SendQueuePackets)]));
    m_pRegistry->Set(GaugeMetric::Net_SendQueueBytes, static_cast<double>(gauges[static_cast<size_t>(netlib::StatGauge::SendQueueBytes)]));
    m_pRegistry->Set(GaugeMetric::Net_SendInFlightPackets, static_cast<double>(gauges[static_cast<size_t>(netlib::StatGauge::SendInFlightPackets)]));
    m_pRegistry->Set(GaugeMetric::Net_SendInFlightBytes, static_cast<double>(gauges[static_cast<size_t>(netlib::StatGauge::SendInFlightBytes)]));

    // PacketPool snapshot은 shard별 free-list lock이 필요하므로 매 scrape마다 갱신하지 않는다.
    // Prometheus scrape 주기가 30초보다 짧으면 그 사이 요청들은 마지막으로 게시한 PacketPool 값만 읽는다.
    const auto now = std::chrono::steady_clock::now();
    if (now >= m_nextPacketPoolSnapshot)
    {
        // 첫 scrape에서는 즉시 게시하고 이후에는 30초 동안 이전 snapshot을 재사용한다.
        publishPacketPool();
        m_nextPacketPoolSnapshot = now + PacketPoolSnapshotInterval;
    }
}

void NetMetricsPublisher::publishPacketPool()
{
    // GetStats가 각 shard를 잠그는 동안에도 실제 allocation은 다른 shard에서 진행될 수 있다.
    // 따라서 이 값은 진단용 근사 snapshot이며 bucket 전체를 하나의 원자적 시점으로 보장하지 않는다.
    const std::vector<netlib::PacketPool::BucketStats> stats = m_pIoContext->GetPacketPool().GetStats();
    uint64 totalAlloc = 0;
    uint64 totalFree = 0;
    uint64 totalCreated = 0;
    uint64 totalScanMiss = 0;
    uint64 totalHeld = 0;
    double maxShardImbalance = 0.0;
    for (const netlib::PacketPool::BucketStats& stat : stats)
    {
        totalAlloc += stat.allocCount;
        totalFree += stat.freeCount;
        totalCreated += stat.newCount;
        totalScanMiss += stat.scanMissCount;
        totalHeld += stat.held;
        const auto [minIter, maxIter] = std::minmax_element(stat.shardHeld.begin(), stat.shardHeld.end());
        const double imbalance = stat.held > 0 ? static_cast<double>(*maxIter - *minIter) / stat.held : 0.0;
        maxShardImbalance = std::max(maxShardImbalance, imbalance);
    }

    m_pRegistry->Set(CounterMetric::PacketPool_Allocations, totalAlloc);
    m_pRegistry->Set(CounterMetric::PacketPool_Frees, totalFree);
    m_pRegistry->Set(CounterMetric::PacketPool_CreatedPackets, totalCreated);
    m_pRegistry->Set(CounterMetric::PacketPool_ScanMisses, totalScanMiss);
    m_pRegistry->Set(GaugeMetric::PacketPool_Held, static_cast<double>(totalHeld));
    m_pRegistry->Set(GaugeMetric::PacketPool_InUse, static_cast<double>(totalAlloc >= totalFree ? totalAlloc - totalFree : 0));
    m_pRegistry->Set(GaugeMetric::PacketPool_HitRatio, totalAlloc == 0 ? 1.0 : static_cast<double>(totalAlloc - totalCreated) / totalAlloc);
    m_pRegistry->Set(GaugeMetric::PacketPool_ShardImbalanceRatio, maxShardImbalance);
}

} // namespace serverbase
