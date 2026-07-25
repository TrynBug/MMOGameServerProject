#include "pch.h"
#include "ContentsThread.h"
#include "Logger.h"

namespace serverbase
{

ContentsThread::ContentsThread(int64 updateIntervalMs)
    : m_updateIntervalMs(updateIntervalMs)
{
}

ContentsThread::~ContentsThread()
{
    Stop();
}

bool ContentsThread::InitializeMetrics(MetricsRegistry& registry, bool registerMetrics)
{
    // 1ms의 짧은 tick부터 250ms의 심각한 stall까지 한 histogram으로 확인한다.
    // configured update interval(일반적으로 50ms) 주변에 경계를 촘촘하게 두어 정상/overrun을 구분한다.
    const std::vector<double> tickBuckets = { 0.001, 0.0025, 0.005, 0.010, 0.020, 0.040, 0.050, 0.075, 0.100, 0.250 };

    // container/queue 크기는 scrape 직전 전체 합계로 갱신하고, 처리량/예외/tick과 시간 분포는 모든 thread가 같은 series에 누적한다.
    m_pMetricsRegistry = &registry;
    if (!registerMetrics)
        return true;

    bool registered = true;
    registered &= registry.AddGauge(GaugeMetric::ContentsThreads_ContentsTotal, "mmo_contents_count", "Contents currently owned by all ContentsThreads.");
    registered &= registry.AddGauge(GaugeMetric::ContentsThreads_PendingAddTotal, "mmo_contents_pending_add", "Contents waiting to be added across all ContentsThreads.");
    registered &= registry.AddGauge(GaugeMetric::ContentsThreads_PendingRemoveTotal, "mmo_contents_pending_remove", "Contents waiting to be removed across all ContentsThreads.");
    registered &= registry.AddGauge(GaugeMetric::ContentsThreads_TaskQueueDepthTotal, "mmo_contents_task_queue_depth", "Tasks waiting across all ContentsThreads.");
    registered &= registry.AddGauge(GaugeMetric::ContentsThreads_TaskOldestAgeSecondsMax, "mmo_contents_task_oldest_age_seconds", "Oldest queued task age among all ContentsThreads.");
    registered &= registry.AddCounter(CounterMetric::ContentsThreads_TasksPosted, "mmo_contents_tasks_posted_total", "Tasks posted to all ContentsThreads.");
    registered &= registry.AddCounter(CounterMetric::ContentsThreads_TasksProcessed, "mmo_contents_tasks_processed_total", "Tasks processed by all ContentsThreads.");
    registered &= registry.AddCounter(CounterMetric::ContentsThreads_TaskExceptions, "mmo_contents_task_exceptions_total", "Exceptions caught while running ContentsThread tasks.");
    registered &= registry.AddCounter(CounterMetric::ContentsThreads_UpdateExceptions, "mmo_contents_update_exceptions_total", "Exceptions caught while updating Contents.");
    registered &= registry.AddCounter(CounterMetric::ContentsThreads_TickOverruns, "mmo_contents_tick_overrun_total", "Overrun ticks across all ContentsThreads.");
    registered &= registry.AddCounter(CounterMetric::ContentsThreads_Ticks, "mmo_contents_ticks_total", "Ticks executed by all ContentsThreads.");
    registered &= registry.AddHistogram(HistogramMetric::ContentsThreads_TickDurationSeconds, "mmo_contents_tick_duration_seconds", "Work duration of ContentsThread ticks.", tickBuckets);
    registered &= registry.AddHistogram(HistogramMetric::ContentsThreads_TickDelaySeconds, "mmo_contents_tick_delay_seconds", "Scheduled-start delay of ContentsThread ticks.", tickBuckets);
    return registered;
}

ContentsThread::MetricsSnapshot ContentsThread::GetMetricsSnapshot() const
{
    MetricsSnapshot snapshot;
    snapshot.contentsCount = m_metricContentsCount.load(std::memory_order_relaxed);
    snapshot.pendingAdd = m_metricPendingAdd.load(std::memory_order_relaxed);
    snapshot.pendingRemove = m_metricPendingRemove.load(std::memory_order_relaxed);
    snapshot.taskQueueDepth = m_metricTaskQueueDepth.load(std::memory_order_relaxed);
    snapshot.taskOldestAgeSeconds = m_metricTaskOldestAgeSeconds.load(std::memory_order_relaxed);
    return snapshot;
}

void ContentsThread::Start()
{
    m_bRunning = true;
    m_thread   = std::thread(&ContentsThread::threadProc, this);
}

void ContentsThread::Stop()
{
    m_bRunning = false;
    if (m_thread.joinable())
        m_thread.join();
}

void ContentsThread::AddContents(ContentsPtr spContents)
{
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    m_pendingAdd.push_back(std::move(spContents));
    // producer thread에서 enqueue와 함께 증가시키고 owner thread가 실제 container로 옮긴 뒤 0으로 게시한다.
    m_metricPendingAdd.store(static_cast<int64>(m_pendingAdd.size()), std::memory_order_relaxed);
}

void ContentsThread::RemoveContents(ContentsPtr spContents)
{
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    m_pendingRemove.push_back(std::move(spContents));
    m_metricPendingRemove.store(static_cast<int64>(m_pendingRemove.size()), std::memory_order_relaxed);
}

int32 ContentsThread::GetContentsCount() const
{
    std::lock_guard<std::mutex> lock(m_contentsMutex);
    return static_cast<int32>(m_contents.size());
}

void ContentsThread::Post(std::function<void()> fn)
{
    std::lock_guard<std::mutex> lock(m_taskMutex);
    // monitoring이 꺼졌으면 timestamp 저장을 생략한다. 켜졌을 때만 oldest-age 계산에 필요한 enqueue 시각을 보관한다.
    const auto enqueuedAt = m_pMetricsRegistry != nullptr ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    m_tasks.push_back(TaskEntry { std::move(fn), enqueuedAt });
    m_metricTaskQueueDepth.store(static_cast<int64>(m_tasks.size()), std::memory_order_relaxed);
    if (m_pMetricsRegistry != nullptr)
        m_pMetricsRegistry->Inc(CounterMetric::ContentsThreads_TasksPosted);
}

// executor 컴포넌트는 소유 ContentsThread의 태스크 큐로 위임한다(여기서 ContentsThread 완전타입 사용).
void ContentsThreadResumeExecutor::Post(std::function<void()> fn)
{
    m_owner.Post(std::move(fn));
}

void ContentsThread::threadProc()
{
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    TimePoint prevTime = Clock::now();
    TimePoint scheduledStart = prevTime;

    while (m_bRunning)
    {
        TimePoint now = Clock::now();
        // delay는 이전 tick의 작업 시간 자체가 아니라 예정된 시작 시각보다 실제 thread가 늦게 깨어난 시간이다.
        // OS scheduling 지연이나 다른 thread의 CPU 점유가 커질 때 duration과 독립적으로 증가할 수 있다.
        const double tickDelaySeconds = std::max(0.0, std::chrono::duration<double>(now - scheduledStart).count());
        if (m_pMetricsRegistry != nullptr)
            m_pMetricsRegistry->Observe(HistogramMetric::ContentsThreads_TickDelaySeconds, tickDelaySeconds);
        int64 deltaMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - prevTime).count();
        prevTime = now;

        // 추가/제거 대기중인 컨텐츠 처리
        {
            std::lock_guard<std::mutex> pendingLock(m_pendingMutex);

            // 추가 대기중인 컨텐츠 추가
            if (!m_pendingAdd.empty())
            {
                std::lock_guard<std::mutex> contentsLock(m_contentsMutex);
                for (ContentsPtr& spContents : m_pendingAdd)
                {
                    spContents->Start();
                    m_contents.push_back(std::move(spContents));
                }

                m_pendingAdd.clear();
                m_metricPendingAdd.store(0, std::memory_order_relaxed);
            }

            // 제거 대기중인 컨텐츠 제거.
            // 단, IsBusy()(진행 중인 비동기 후속작업 등)인 컨텐츠는 이번 tick엔 제거하지 않고 보류한다.
            // 보류 중에도 Update와 태스크 큐 drain은 계속되어 in-flight가 0으로 수렴 → 다음 tick에 제거된다.
            if (!m_pendingRemove.empty())
            {
                std::lock_guard<std::mutex> contentsLock(m_contentsMutex);
                std::vector<ContentsPtr> stillBusy;
                for (ContentsPtr& spContents : m_pendingRemove)
                {
                    auto iter = std::find(m_contents.begin(), m_contents.end(), spContents);
                    if (iter == m_contents.end())
                        continue;   // 이미 없음

                    if ((*iter)->IsBusy())
                    {
                        stillBusy.push_back(spContents);   // 보류 → 다음 tick 재시도
                        continue;
                    }

                    (*iter)->Stop();
                    m_contents.erase(iter);
                }

                m_pendingRemove = std::move(stillBusy);
                m_metricPendingRemove.store(static_cast<int64>(m_pendingRemove.size()), std::memory_order_relaxed);
            }
        }

        // 큐잉된 태스크(코루틴 resume 등) 실행
        {
            std::vector<TaskEntry> tasks;
            {
                std::lock_guard<std::mutex> taskLock(m_taskMutex);
                tasks.swap(m_tasks);
                m_metricTaskQueueDepth.store(static_cast<int64>(m_tasks.size()), std::memory_order_relaxed);
            }
            for (TaskEntry& task : tasks)
            {
                try
                {
                    task.fn();
                }
                catch (const std::exception& e)
                {
                    if (m_pMetricsRegistry != nullptr)
                        m_pMetricsRegistry->Inc(CounterMetric::ContentsThreads_TaskExceptions);
                    LOG_WRITE(LogLevel::Error, std::format("ContentsThread::task exception: {}", e.what()));
                }
            }
            if (m_pMetricsRegistry != nullptr && !tasks.empty())
                // 한 cycle에 drain한 개수만큼 누적해 posted-processed 차이로 아직 처리되지 않은 작업 흐름도 교차 확인할 수 있다.
                m_pMetricsRegistry->Inc(CounterMetric::ContentsThreads_TasksProcessed, static_cast<uint64>(tasks.size()));
        }

        // 컨텐츠 업데이트
        {
            std::lock_guard<std::mutex> contentsLock(m_contentsMutex);
            for (ContentsPtr& spContents : m_contents)
            {
                try
                {
                    spContents->Update(deltaMs);
                }
                catch (const std::exception& e)
                {
                    if (m_pMetricsRegistry != nullptr)
                        m_pMetricsRegistry->Inc(CounterMetric::ContentsThreads_UpdateExceptions);
                    LOG_WRITE(LogLevel::Error, std::format("ContentsThread::Update exception: {}", e.what()));
                }
            }
            m_metricContentsCount.store(static_cast<int64>(m_contents.size()), std::memory_order_relaxed);
        }

        if (m_pMetricsRegistry != nullptr)
        {
            // depth만으로는 오래 굶은 작업을 찾기 어렵다. queue front의 age를 함께 게시해 starvation을 탐지한다.
            std::lock_guard<std::mutex> taskLock(m_taskMutex);
            const double oldestAge = m_tasks.empty() ? 0.0 : std::chrono::duration<double>(Clock::now() - m_tasks.front().enqueuedAt).count();
            m_metricTaskOldestAgeSeconds.store(oldestAge, std::memory_order_relaxed);
        }

        // 다음 tick까지 대기
        TimePoint nextTick = now + std::chrono::milliseconds(m_updateIntervalMs);
        TimePoint afterUpdate = Clock::now();
        const double tickDurationSeconds = std::chrono::duration<double>(afterUpdate - now).count();
        if (m_pMetricsRegistry != nullptr)
            m_pMetricsRegistry->Inc(CounterMetric::ContentsThreads_Ticks);
        if (m_pMetricsRegistry != nullptr)
            m_pMetricsRegistry->Observe(HistogramMetric::ContentsThreads_TickDurationSeconds, tickDurationSeconds);
        // duration이 설정 주기를 넘으면 다음 tick sleep 여유가 없다는 뜻이다. 누적 Counter이므로 rate()로 지속 과부하 여부를 본다.
        if (m_pMetricsRegistry != nullptr && tickDurationSeconds * 1000.0 > static_cast<double>(m_updateIntervalMs))
            m_pMetricsRegistry->Inc(CounterMetric::ContentsThreads_TickOverruns);
        scheduledStart = nextTick;
        if (afterUpdate < nextTick)
        {
            std::this_thread::sleep_until(nextTick);
        }
    }

    // 스레드 종료 시 모든 컨텐츠 정지
    std::lock_guard<std::mutex> contentsLock(m_contentsMutex);
    for (auto& spContents : m_contents)
    {
        spContents->Stop();
    }

    m_contents.clear();
    m_metricContentsCount.store(0, std::memory_order_relaxed);
    m_metricPendingAdd.store(0, std::memory_order_relaxed);
    m_metricPendingRemove.store(0, std::memory_order_relaxed);
    m_metricTaskQueueDepth.store(0, std::memory_order_relaxed);
    m_metricTaskOldestAgeSeconds.store(0.0, std::memory_order_relaxed);
}

} // namespace serverbase
