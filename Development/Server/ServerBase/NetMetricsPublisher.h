#pragma once

#include "MetricsRegistry.h"
#include "NetLibStats.h"
#include "PacketPool.h"

#include <chrono>

namespace netlib
{
class IoContext;
}

namespace serverbase
{

// NetLib의 고빈도 통계를 scrape 시점에 Prometheus metric으로 복사하는 adapter다.
// packet 송수신 hot path는 이 클래스나 Registry를 직접 호출하지 않고 NetLibStats만 갱신하므로
// Prometheus 요청 빈도가 network 처리 비용과 lock 경합을 늘리지 않는다.
class NetMetricsPublisher
{
public:
    // NetLib enum에 대응하는 공용 metric enum과 PacketPool capacity별 고정 series를 서버 시작 시 한 번 등록한다.
    bool Initialize(MetricsRegistry& registry, netlib::IoContext& ioContext);

    // /metrics 직전에 호출한다. NetLibStats는 매번 복사하지만 lock이 필요한 PacketPool은 30초 snapshot을 재사용한다.
    void Publish();

private:
    bool registerNetMetrics(MetricsRegistry& registry);
    bool registerPacketPoolMetrics(MetricsRegistry& registry);
    void publishPacketPool();

    // IoContext는 소유하지 않으며 ServerBase가 Publisher보다 긴 수명을 보장한다.
    netlib::IoContext* m_pIoContext = nullptr;
    MetricsRegistry* m_pRegistry = nullptr;
    // system clock 변경의 영향을 받지 않도록 steady_clock 기준의 다음 저빈도 수집 시각을 저장한다.
    std::chrono::steady_clock::time_point m_nextPacketPoolSnapshot{};
};

} // namespace serverbase
