#pragma once

#include "MetricsRegistry.h"

namespace serverbase
{

// 별도 windows_exporter 없이 현재 서버 process와 Windows host 상태를 /metrics에 게시한다.
// process metric은 모든 서버가 제공하고, host metric은 중복 수집을 피하기 위해 RegistryServer만 제공한다.
class WindowsMetricsPublisher
{
public:
    // publishHostMetrics=false인 process도 process metric은 게시한다.
    // host metric은 같은 머신에서 여러 서버가 중복 series를 만들지 않도록 RegistryServer 한 곳만 등록한다.
    bool Initialize(MetricsRegistry& registry, bool publishHostMetrics);

    // /metrics scrape 직전에 호출한다. 사용 API는 현재 process/host의 누적값과 gauge를 읽는 저비용 API로 제한한다.
    void Publish();

private:
    void publishProcessMetrics();
    void publishHostMetrics();

private:
    bool m_publishHostMetrics = false;
    MetricsRegistry* m_pRegistry = nullptr;
};

} // namespace serverbase
