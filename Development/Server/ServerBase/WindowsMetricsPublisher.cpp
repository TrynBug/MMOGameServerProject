#include "pch.h"
#include "WindowsMetricsPublisher.h"

#include <psapi.h>

namespace serverbase
{
namespace
{
constexpr double FileTimeTicksPerSecond = 10'000'000.0;

uint64 FileTimeToTicks(const FILETIME& value)
{
    // FILETIME은 32bit low/high로 나뉜 100ns tick이므로 64bit 정수로 합친 뒤 초 단위로 변환한다.
    ULARGE_INTEGER ticks{};
    ticks.LowPart = value.dwLowDateTime;
    ticks.HighPart = value.dwHighDateTime;
    return ticks.QuadPart;
}
}

bool WindowsMetricsPublisher::Initialize(MetricsRegistry& registry, bool publishHostMetrics)
{
    m_publishHostMetrics = publishHostMetrics;
    m_pRegistry = &registry;

    // CPU 시간은 process 시작 이후의 kernel+user 누적 초다. 현재 Registry의 정수 Counter가 소수 초를 표현하지 못하므로
    // cumulative Gauge로 게시하며 Grafana에서는 rate()로 사용률을 계산한다.
    bool registered = true;
    registered &= registry.AddGauge(GaugeMetric::Windows_ProcessCpuSeconds, "mmo_process_cpu_seconds", "Cumulative kernel and user CPU time consumed by this server process.");
    // memory/handle은 현재값이므로 Gauge, Windows가 process 시작부터 누적하는 I/O byte는 Counter로 노출한다.
    registered &= registry.AddGauge(GaugeMetric::Windows_ProcessWorkingSetBytes, "mmo_process_working_set_bytes", "Physical memory currently mapped into this server process.");
    registered &= registry.AddGauge(GaugeMetric::Windows_ProcessPeakWorkingSetBytes, "mmo_process_peak_working_set_bytes", "Peak physical memory mapped into this server process.");
    registered &= registry.AddGauge(GaugeMetric::Windows_ProcessPrivateBytes, "mmo_process_private_bytes", "Private committed memory of this server process.");
    registered &= registry.AddGauge(GaugeMetric::Windows_ProcessHandleCount, "mmo_process_handle_count", "Open Windows handle count of this server process.");
    registered &= registry.AddCounter(CounterMetric::Windows_ProcessIoReadBytes, "mmo_process_io_read_bytes_total", "Bytes read by this server process since it started.");
    registered &= registry.AddCounter(CounterMetric::Windows_ProcessIoWriteBytes, "mmo_process_io_write_bytes_total", "Bytes written by this server process since it started.");
    registered &= registry.AddCounter(CounterMetric::Windows_ProcessIoOtherBytes, "mmo_process_io_other_bytes_total", "Bytes transferred by non-read/write operations since this server process started.");
    registered &= registry.AddCounter(CounterMetric::Windows_ProcessCollectionErrors, "mmo_windows_metrics_collection_errors_total", "Windows API metric collection failures by scope.",
        { { "scope", "process" } });
    if (!registered)
        return false;

    if (!m_publishHostMetrics)
        return true;

    // host CPU는 idle/system/user 세 고정 mode만 등록한다. CPU core별 series는 만들지 않아 host cardinality를 일정하게 유지한다.
    registered &= registry.AddGauge(GaugeMetric::Windows_HostCpuIdleSeconds, "mmo_host_cpu_seconds", "Cumulative Windows host CPU time by mode.", { { "mode", "idle" } });
    registered &= registry.AddGauge(GaugeMetric::Windows_HostCpuSystemSeconds, "mmo_host_cpu_seconds", "Cumulative Windows host CPU time by mode.", { { "mode", "system" } });
    registered &= registry.AddGauge(GaugeMetric::Windows_HostCpuUserSeconds, "mmo_host_cpu_seconds", "Cumulative Windows host CPU time by mode.", { { "mode", "user" } });
    registered &= registry.AddGauge(GaugeMetric::Windows_HostMemoryTotalBytes, "mmo_host_memory_total_bytes", "Total physical memory installed on the Windows host.");
    registered &= registry.AddGauge(GaugeMetric::Windows_HostMemoryAvailableBytes, "mmo_host_memory_available_bytes", "Physical memory currently available on the Windows host.");
    registered &= registry.AddCounter(CounterMetric::Windows_HostCollectionErrors, "mmo_windows_metrics_collection_errors_total", "Windows API metric collection failures by scope.",
        { { "scope", "host" } });
    return registered;
}

void WindowsMetricsPublisher::Publish()
{
    publishProcessMetrics();
    if (m_publishHostMetrics)
        publishHostMetrics();
}

void WindowsMetricsPublisher::publishProcessMetrics()
{
    // 한 scrape cycle 안에서 API별 성공 여부를 모으되, 실패한 API의 기존 metric 값은 마지막 정상값으로 유지한다.
    // 오류 Counter가 함께 증가하므로 dashboard는 stale 값과 수집 실패를 구분할 수 있다.
    const HANDLE process = ::GetCurrentProcess();
    bool success = true;

    FILETIME creationTime{};
    FILETIME exitTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};
    if (::GetProcessTimes(process, &creationTime, &exitTime, &kernelTime, &userTime))
    {
        const uint64 cpuTicks = FileTimeToTicks(kernelTime) + FileTimeToTicks(userTime);
        m_pRegistry->Set(GaugeMetric::Windows_ProcessCpuSeconds, static_cast<double>(cpuTicks) / FileTimeTicksPerSecond);
    }
    else
    {
        success = false;
    }

    // WorkingSetSize는 현재 resident physical memory이고 PrivateUsage는 다른 process와 공유하지 않는 commit 크기다.
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (::GetProcessMemoryInfo(process, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
    {
        m_pRegistry->Set(GaugeMetric::Windows_ProcessWorkingSetBytes, static_cast<double>(memory.WorkingSetSize));
        m_pRegistry->Set(GaugeMetric::Windows_ProcessPeakWorkingSetBytes, static_cast<double>(memory.PeakWorkingSetSize));
        m_pRegistry->Set(GaugeMetric::Windows_ProcessPrivateBytes, static_cast<double>(memory.PrivateUsage));
    }
    else
    {
        success = false;
    }

    DWORD handleCount = 0;
    if (::GetProcessHandleCount(process, &handleCount))
        m_pRegistry->Set(GaugeMetric::Windows_ProcessHandleCount, static_cast<double>(handleCount));
    else
        success = false;

    IO_COUNTERS ioCounters{};
    if (::GetProcessIoCounters(process, &ioCounters))
    {
        // Windows가 process 시작 이후 누적한 절대값을 복사한다. scrape마다 Inc하면 같은 값이 중복 합산되므로 Set을 사용한다.
        m_pRegistry->Set(CounterMetric::Windows_ProcessIoReadBytes, ioCounters.ReadTransferCount);
        m_pRegistry->Set(CounterMetric::Windows_ProcessIoWriteBytes, ioCounters.WriteTransferCount);
        m_pRegistry->Set(CounterMetric::Windows_ProcessIoOtherBytes, ioCounters.OtherTransferCount);
    }
    else
    {
        success = false;
    }

    // 한 scrape에서 여러 API가 실패해도 collection cycle 한 번으로 집계해 error rate를 과장하지 않는다.
    if (!success)
        m_pRegistry->Inc(CounterMetric::Windows_ProcessCollectionErrors);
}

void WindowsMetricsPublisher::publishHostMetrics()
{
    bool success = true;

    FILETIME idleTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};
    if (::GetSystemTimes(&idleTime, &kernelTime, &userTime))
    {
        const uint64 idleTicks = FileTimeToTicks(idleTime);
        const uint64 kernelTicks = FileTimeToTicks(kernelTime);
        const uint64 userTicks = FileTimeToTicks(userTime);
        // GetSystemTimes의 kernel에는 idle이 포함된다. 그대로 게시하면 system 사용량이 중복되므로 idle을 뺀다.
        // 드문 관측 순서/overflow 상황에서 unsigned underflow가 나지 않도록 0에서 포화시킨다.
        const uint64 systemTicks = kernelTicks >= idleTicks ? kernelTicks - idleTicks : 0;

        m_pRegistry->Set(GaugeMetric::Windows_HostCpuIdleSeconds, static_cast<double>(idleTicks) / FileTimeTicksPerSecond);
        m_pRegistry->Set(GaugeMetric::Windows_HostCpuSystemSeconds, static_cast<double>(systemTicks) / FileTimeTicksPerSecond);
        m_pRegistry->Set(GaugeMetric::Windows_HostCpuUserSeconds, static_cast<double>(userTicks) / FileTimeTicksPerSecond);
    }
    else
    {
        success = false;
    }

    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (::GlobalMemoryStatusEx(&memory))
    {
        m_pRegistry->Set(GaugeMetric::Windows_HostMemoryTotalBytes, static_cast<double>(memory.ullTotalPhys));
        m_pRegistry->Set(GaugeMetric::Windows_HostMemoryAvailableBytes, static_cast<double>(memory.ullAvailPhys));
    }
    else
    {
        success = false;
    }

    if (!success)
        m_pRegistry->Inc(CounterMetric::Windows_HostCollectionErrors);
}

} // namespace serverbase
