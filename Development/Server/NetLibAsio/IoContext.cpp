#include "IoContext.h"

#include <boost/asio.hpp>
#include <algorithm>
#include <map>
#include <thread>
#include <vector>

namespace netlib
{

namespace asio = boost::asio;

struct IoContext::Impl
{
    explicit Impl(int32 numConcurrentThread)
        : context(numConcurrentThread > 0 ? numConcurrentThread : 0)
    {
    }

    // 가장 빠른 작업에 맞춰 타이머를 등록한다.
    // 주의: 이 함수는 delayMutex를 획득한 다음 호출해야 한다.
    void armTimer()
    {
        // 등록된 작업 중 가장 빠른 시간 얻기
        const auto deliverAt = delayed.begin()->first;

        // 타이머를 가장 빠른 시간에 맞춘다.
        timer.expires_at(deliverAt);

        // 타이머 등록
        timer.async_wait([this](const boost::system::error_code& error)
        {
            // 타이머 콜백함수
            if (error)
                return;

            std::unique_lock lock(delayMutex);

            // 등록된 작업이 있는지 체크한다. 있다면 시작시간에 도달했는지 확인한다.
            while (!delayed.empty() && delayed.begin()->first <= std::chrono::steady_clock::now())
            {
                // 가장 첫 작업을 하나 꺼낸다.
                auto item = delayed.extract(delayed.begin());

                // 다른 스레드에서 ScheduleAt()을 호출할 수 있도록 작업을 실행하는 동안에는 lock을 푼다.
                lock.unlock();

                // 작업 실행
                item.mapped()();

                // 다시 lock
                lock.lock();
            }

            if (!delayed.empty())
            {
                // 작업이 있는데 아직 시작시간에 도달하지 않은 경우이다. 타이머를 다시 등록한다.
                armTimer();
            }
            else
            {
                // 타이머가 등록되어 있지 않음을 표시함
                timerArmed = false;
            }
        });
    }

    asio::io_context context; // 소켓 I/O, 타이머, 게시 작업을 처리하는 공용 이벤트 루프
    // 대기 중인 I/O가 비는 동안에도 워커의 run()을 유지한다.
    asio::executor_work_guard<asio::io_context::executor_type> work{ asio::make_work_guard(context) };
    // 워커가 여러 개여도 예약 작업의 타이머 완료 핸들러는 한 번에 하나씩 실행한다.
    asio::strand<asio::io_context::executor_type> delayStrand{ asio::make_strand(context) };
    asio::steady_timer timer{ delayStrand }; // 예약 작업 중 가장 이른 시각을 기다리는 타이머
    std::mutex delayMutex;                  // ScheduleAt(), 타이머 완료, 종료가 공유하는 예약 상태 보호
    std::multimap<std::chrono::steady_clock::time_point, std::function<void()>> delayed; // 실행 시각별 예약 작업
    bool timerArmed = false;                // 타이머가 예약 큐를 처리 중이거나 만료를 기다리는 상태
    std::vector<std::thread> workers;       // 공용 context.run()을 실행하는 워커들
    std::vector<std::weak_ptr<std::function<void()>>> endpoints; // 초기화된 리스너와 클라이언트의 종료 작업
    std::thread::id shutdownThread;        // 종료 콜백에서 같은 스레드가 재진입하는 경우를 구분
};

IoContext::IoContext() = default;

IoContext::~IoContext()
{
    Shutdown();
}

bool IoContext::Initialize(const IoContextConfig& config)
{
    std::lock_guard lock(m_mutex);
    if (m_impl)
        return false;

    m_config = config;
    m_packetPool.Initialize(config.initPacketSize, config.maxPacketSize);
    try
    {
        m_impl = std::make_unique<Impl>(config.numConcurrentThread);
        const auto cores = static_cast<int32>(std::thread::hardware_concurrency());
        const int32 numWorkers = config.numWorkerThread > 0 ? config.numWorkerThread : (cores > 0 ? cores * 2 : 4);
        for (int32 i = 0; i < numWorkers; ++i)
            m_impl->workers.emplace_back([impl = m_impl.get()]() { impl->context.run(); });
    }
    catch (const std::system_error&)
    {
        if (m_impl)
        {
            m_impl->work.reset();
            for (auto& worker : m_impl->workers)
                worker.join();
            m_impl.reset();
        }
        return false;
    }
    m_bRunning.store(true);
    return true;
}

void IoContext::Shutdown()
{
    Impl* impl;
    std::vector<std::weak_ptr<std::function<void()>>> endpoints;
    decltype(Impl::delayed) delayed;
    {
        std::unique_lock lock(m_mutex);
        if (!m_bRunning.exchange(false))
        {
            // 종료 콜백이 같은 스레드에서 재진입하면 자신의 종료를 기다리지 않는다.
            // 다른 호출자는 진행 중인 종료가 끝날 때까지 기다린다.
            if (m_impl && m_impl->shutdownThread == std::this_thread::get_id())
                return;
            m_shutdownCv.wait(lock, [this]() { return !m_impl || IsRunning(); });
            return;
        }
        impl = m_impl.get();
        impl->shutdownThread = std::this_thread::get_id();
        endpoints.swap(impl->endpoints);
        {
            std::lock_guard delayLock(impl->delayMutex);
            delayed.swap(impl->delayed);
            impl->timer.cancel();
        }
    }
    // 예약 함수가 잡고 있던 객체는 소멸 중 네트워크 API를 호출할 수 있으므로 락 밖에서 해제한다.
    delayed.clear();

    // 리스너와 클라이언트가 소켓과 타이머를 닫을 때까지 Impl을 유지한다.
    // 종료 콜백이 컨텍스트 API를 호출할 수 있으므로 m_mutex 밖에서 실행한다.
    for (const auto& endpoint : endpoints)
    {
        if (auto spShutdown = endpoint.lock())
            (*spShutdown)();
    }

    // work guard를 해제하면 run()은 남은 I/O 취소 완료와 게시 작업을 처리한 뒤 반환한다.
    impl->work.reset();
    for (auto& worker : impl->workers)
        worker.join();

    {
        std::lock_guard lock(m_mutex);
        m_impl.reset();
    }
    m_shutdownCv.notify_all();
}

bool IoContext::registerEndpoint(const std::shared_ptr<std::function<void()>>& spShutdown)
{
    // 종료 시작과 같은 락으로 검사한다. 등록에 성공한 객체는 종료 목록에 포함된다.
    std::lock_guard lock(m_mutex);
    if (!IsRunning())
        return false;

    auto& endpoints = m_impl->endpoints;
    // 이미 파괴된 리스너와 클라이언트는 종료 목록에서 제거한다.
    std::erase_if(endpoints, [](const auto& endpoint) { return endpoint.expired(); });
    endpoints.emplace_back(spShutdown);
    return true;
}

asio::io_context& IoContext::getContext()
{
    return m_impl->context;
}

// 작업을 작업큐에 등록한다.
void IoContext::PostMsg(std::function<void()> fn)
{
    if (IsRunning() && fn)
        asio::post(m_impl->context, std::move(fn));
}

// 작업을 스케줄링하고, 타이머를 등록한다.
void IoContext::ScheduleAt(std::chrono::steady_clock::time_point deliverAt, std::function<void()> fn)
{
    if (!IsRunning() || !fn)
        return;

    auto& impl = *m_impl;

    std::lock_guard delayLock(impl.delayMutex);

    // 작업 큐(멀티맵)에 시작시간과 작업을 삽입한다.
    const auto item = impl.delayed.emplace(deliverAt, std::move(fn));

    // 타이머가 등록되어있지 않으면 타이머를 등록한다.
    // 만약 삽입된 작업이 가장 빠른 작업이라면 기존 타이머를 중지하고 타이머를 새로 등록한다.
    if (!impl.timerArmed || item == impl.delayed.begin())
    {
        impl.timerArmed = true;
        impl.armTimer();
    }
}

} // namespace netlib
