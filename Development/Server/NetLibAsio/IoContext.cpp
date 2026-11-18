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
public:
    explicit Impl(int32 numConcurrentThread)
        : context(numConcurrentThread > 0 ? numConcurrentThread : 0)
    {
    }

    // 가장 빠른 작업에 맞춰 타이머를 등록한다.
    // 주의: 이 함수는 timerMutex를 획득한 다음 호출해야 한다.
    void armTimer()
    {
        // 등록된 작업 중 가장 빠른 시간 얻기
        const auto deliverAt = scheduledTasks.begin()->first;

        // 타이머를 가장 빠른 시간에 맞춘다.
        timer.expires_at(deliverAt);

        // 타이머 등록
        timer.async_wait([this](const boost::system::error_code& error)
        {
            // 타이머 콜백함수
            if (error)
                return;

            std::unique_lock lock(timerMutex);

            // 등록된 작업이 있는지 체크한다. 있다면 시작시간에 도달했는지 확인한다.
            while (!scheduledTasks.empty() && scheduledTasks.begin()->first <= std::chrono::steady_clock::now())
            {
                // 가장 첫 작업을 하나 꺼낸다.
                auto item = scheduledTasks.extract(scheduledTasks.begin());

                // 다른 스레드에서 ScheduleAt()을 호출할 수 있도록 작업을 실행하는 동안에는 lock을 푼다.
                lock.unlock();

                // 작업 실행
                item.mapped()();

                // 다시 lock
                lock.lock();
            }

            if (!scheduledTasks.empty())
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

public:
    asio::io_context context; // 소켓 I/O, 타이머, post된 작업이 워커스레드에서 실행되도록 관리하는 객체
    asio::executor_work_guard<asio::io_context::executor_type> work{ asio::make_work_guard(context) }; // 워커스레드가 처리할 작업이 없어도 context.run()이 리턴되지 않도록 해준다.

    std::vector<std::thread> workers;       // 워커스레드. context.run()을 실행한다.
    
    std::mutex timerMutex;                   // scheduledTasks, timer, timerArmed 를 보호하는 mutex
    std::multimap<std::chrono::steady_clock::time_point, std::function<void()>> scheduledTasks; // 예약된 작업. Key=시작시간, Value=작업
    asio::strand<asio::io_context::executor_type> timerStrand{ asio::make_strand(context) }; // timer의 콜백들이 동시에 실행되지 않도록 직렬화한다.
    asio::steady_timer timer{ timerStrand }; // 타이머. 예약된 작업의 시작시간에 도달하면 깨어난다.
    bool timerArmed = false;                 // 타이머가 등록되어있는지 여부. 등록되어 있다면 타이머가 예약된 작업을 처리하는 중이다.

    std::vector<std::weak_ptr<std::function<void()>>> shutdownCallbacks; // Shutdown될 때 수행할 작업
    std::thread::id shutdownThread;        // Shutdown 작업을 진행중인 스레드
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
        // 스레드 수 설정
        m_impl = std::make_unique<Impl>(config.numConcurrentThread);

        const auto cores = static_cast<int32>(std::thread::hardware_concurrency());
        int32 numWorkers = config.numWorkerThread;
        if (numWorkers == 0)
            numWorkers = cores > 0 ? cores * 2 : 4;

        // worker 스레드 시작
        for (int32 i = 0; i < numWorkers; ++i)
        {
            m_impl->workers.emplace_back([impl = m_impl.get()]()
                {
                    impl->context.run();
                });
        }
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
    std::vector<std::weak_ptr<std::function<void()>>> shutdownCallbacks;
    decltype(Impl::scheduledTasks) scheduledTasks;
    {
        std::unique_lock lock(m_mutex);

        // 종료플래그 설정
        if (!m_bRunning.exchange(false))
        {
            // 종료가 진행중이라면 종료가 끝날 때까지 기다린다.
            // 이 때 종료를 진행중인 스레드가 이 함수에 재진입 한거라면 기다리지 않는다.
            if (m_impl && m_impl->shutdownThread == std::this_thread::get_id())
                return;

            m_shutdownCv.wait(lock, [this]() 
                { 
                    return !m_impl || IsRunning(); 
                });

            return;
        }

        // 종료작업 진행
        impl = m_impl.get();
        impl->shutdownThread = std::this_thread::get_id();
        shutdownCallbacks.swap(impl->shutdownCallbacks);
        {
            std::lock_guard delayLock(impl->timerMutex);
            scheduledTasks.swap(impl->scheduledTasks);
            impl->timer.cancel();
        }
    }

    // 예약된 작업 모두 제거.
    // 작업 함수가 잡고 있던 객체는 소멸 중 네트워크 API를 호출할 수 있으므로 락 밖에서 제거한다.
    scheduledTasks.clear();

    // 종료 시 수행할 작업 수행.
    // shutdown 콜백이 컨텍스트 API를 호출할 수 있으므로 m_mutex 밖에서 실행한다.
    for (const auto& endpoint : shutdownCallbacks)
    {
        if (auto spShutdown = endpoint.lock())
            (*spShutdown)();
    }

    // work guard를 해제한다. 그러면 context.run()은 남은 I/O 취소 완료통지와 post된 작업들을 처리한 뒤 리턴한다.
    impl->work.reset();
    for (auto& worker : impl->workers)
        worker.join();

    {
        std::lock_guard lock(m_mutex);
        m_impl.reset();
    }

    m_shutdownCv.notify_all();
}

// Shutdown 될때 실행할 작업을 등록한다.
bool IoContext::registerShutdownCallback(const std::shared_ptr<std::function<void()>>& spShutdown)
{
    std::lock_guard lock(m_mutex);

    if (!IsRunning())
        return false;

    auto& shutdownCallbacks = m_impl->shutdownCallbacks;

    // 작업의 소유자가 제거되었다면 작업도 제거한다.
    std::erase_if(shutdownCallbacks, [](const auto& shutdownCallback) 
        { 
            return shutdownCallback.expired(); 
        });

    // 작업 등록
    shutdownCallbacks.emplace_back(spShutdown);

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

    std::lock_guard delayLock(impl.timerMutex);

    // 작업 큐(멀티맵)에 시작시간과 작업을 삽입한다.
    const auto item = impl.scheduledTasks.emplace(deliverAt, std::move(fn));

    // 타이머가 등록되어있지 않으면 타이머를 등록한다.
    // 만약 삽입된 작업이 가장 빠른 작업이라면 기존 타이머를 중지하고 타이머를 새로 등록한다.
    if (!impl.timerArmed || item == impl.scheduledTasks.begin())
    {
        impl.timerArmed = true;
        impl.armTimer();
    }
}

} // namespace netlib
