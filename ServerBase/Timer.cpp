#include "pch.h"
#include "Timer.h"
#include "Logger.h"

namespace serverbase
{

void Timer::Start()
{
    m_bRunning = true;
    m_thread   = std::thread(&Timer::threadProc, this);
}

void Timer::Stop()
{
    m_bRunning = false;
    m_cv.notify_all();

    if (m_thread.joinable())
        m_thread.join();

    std::lock_guard<std::mutex> lock(m_mutex);
    m_timers.clear();
}

int32 Timer::Register(int64 intervalMs, std::function<void()> callback)
{
    int32 id = m_nextId.fetch_add(1);

    TimerEntry entry;
    entry.id = id;
    entry.intervalMs = intervalMs;
    entry.callback = std::move(callback);
    entry.nextFireTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(intervalMs);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_timers.push_back(std::move(entry));
    }

    m_cv.notify_all();
    return id;
}

void Timer::Unregister(int32 timerId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_timers.erase(
        std::remove_if(m_timers.begin(), m_timers.end(), [timerId](const TimerEntry& e) { return e.id == timerId; }),
        m_timers.end()
    );
}

void Timer::threadProc()
{
    while (m_bRunning)
    {
        auto now = std::chrono::steady_clock::now();

        // 호출시간이 도달한 타이머 콜백 수집
        std::vector<std::function<void()>> callbacks;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (TimerEntry& entry : m_timers)
            {
                if (now >= entry.nextFireTime)
                {
                    callbacks.push_back(entry.callback);
                    entry.nextFireTime = now + std::chrono::milliseconds(entry.intervalMs);
                }
            }
        }

        // 콜백 호출
        for (auto& callback : callbacks)
        {
            try
            {
                callback();
            }
            catch (const std::exception& e)
            {
                LOG_WRITE(LogLevel::Error, std::string("Timer callback exception: ") + e.what());
            }
        }

        // 100ms 동안 기다린다.
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait_for(lock, std::chrono::milliseconds(100),
            [this] { return !m_bRunning.load(); });
    }
}

} // namespace serverbase
