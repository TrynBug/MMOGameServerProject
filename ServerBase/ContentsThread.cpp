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
}

void ContentsThread::RemoveContents(ContentsPtr spContents)
{
    std::lock_guard<std::mutex> lock(m_pendingMutex);
    m_pendingRemove.push_back(std::move(spContents));
}

int32 ContentsThread::GetContentsCount() const
{
    std::lock_guard<std::mutex> lock(m_contentsMutex);
    return static_cast<int32>(m_contents.size());
}

void ContentsThread::threadProc()
{
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    TimePoint prevTime = Clock::now();

    while (m_bRunning)
    {
        TimePoint now = Clock::now();
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
            }

            // 제거 대기중인 컨텐츠 제거
            if (!m_pendingRemove.empty())
            {
                std::lock_guard<std::mutex> contentsLock(m_contentsMutex);
                for (ContentsPtr& spContents : m_pendingRemove)
                {
                    auto iter = std::find(m_contents.begin(), m_contents.end(), spContents);
                    if (iter != m_contents.end())
                    {
                        (*iter)->Stop();
                        m_contents.erase(iter);
                    }
                }
                m_pendingRemove.clear();
            }
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
                    LOG_WRITE(LogLevel::Error, std::string("ContentsThread::Update exception: ") + e.what());
                }
            }
        }

        // 다음 tick까지 대기
        TimePoint nextTick = now + std::chrono::milliseconds(m_updateIntervalMs);
        TimePoint afterUpdate = Clock::now();
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
}

} // namespace serverbase
