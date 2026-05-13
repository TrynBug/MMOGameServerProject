#pragma once

#include "pch.h"
#include "DBTypes.h"

namespace db
{

// ─────────────────────────────────────────────────────────────────────────────
// IResumeExecutor
//
// co_await 완료 후 코루틴을 어느 스레드에서 재개할지 결정하는 인터페이스.
//
// 사용 예:
//   - CoroutineTest : 자체 스레드 풀(SimpleThreadPool)을 구현해서 전달
//   - 실제 서버    : ServerBase의 IOCP Worker 스레드 풀을 연결
//
// nullptr을 전달하면 DB worker 스레드에서 직접 resume한다.
// (단순 테스트나 Worker 스레드가 안전한 경우에만 사용)
// ─────────────────────────────────────────────────────────────────────────────
class IResumeExecutor
{
public:
    virtual ~IResumeExecutor() = default;
    virtual void Post(std::function<void()> fn) = 0;
};


// ─────────────────────────────────────────────────────────────────────────────
// DBTask<T>
//
// AsyncDBQueue::ExecuteAsync()가 리턴하는 코루틴 타입.
// co_await으로 DBResult를 기다릴 수 있다.
//
// 사용 예:
//   db::DBTask<db::DBResult> MyCoroutine(db::AsyncDBQueue& dbQueue)
//   {
//       db::DBResult r1 = co_await dbQueue.ExecuteAsync("SELECT ...", {});
//       db::DBResult r2 = co_await dbQueue.ExecuteAsync("UPDATE ...", {r1.GetInt64(0, "id")});
//       co_return r2;
//   }
// ─────────────────────────────────────────────────────────────────────────────
template<typename T = void>
class DBTask
{
public:
    // ── promise_type ────────────────────────────────────────────────────────
    struct promise_type
    {
        std::optional<T>         result;
        std::exception_ptr       exception;
        std::coroutine_handle<>  continuation; // 이 태스크를 co_await 하는 바깥 코루틴

        DBTask get_return_object()
        {
            return DBTask{ std::coroutine_handle<promise_type>::from_promise(*this) };
        }

        // 코루틴 시작 시 바로 실행 (suspend 없이)
        std::suspend_never initial_suspend() noexcept { return {}; }

        // 코루틴 종료 시: 바깥 코루틴이 있으면 재개, 없으면 그냥 종료
        struct FinalAwaiter
        {
            bool await_ready() noexcept { return false; }
            void await_resume() noexcept {}

            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept
            {
                auto cont = h.promise().continuation;
                return cont ? cont : std::noop_coroutine();
            }
        };
        FinalAwaiter final_suspend() noexcept { return {}; }

        void return_value(T value) { result = std::move(value); }

        void unhandled_exception() { exception = std::current_exception(); }
    };

    // ── awaitable (co_await DBTask<T> 할 때 사용) ──────────────────────────
    bool await_ready() const noexcept
    {
        return m_handle && m_handle.done();
    }

    void await_suspend(std::coroutine_handle<> caller) noexcept
    {
        m_handle.promise().continuation = caller;
    }

    T await_resume()
    {
        auto& p = m_handle.promise();
        if (p.exception)
            std::rethrow_exception(p.exception);
        return std::move(*p.result);
    }

    // ── 생성/소멸 ────────────────────────────────────────────────────────────
    explicit DBTask(std::coroutine_handle<promise_type> h) : m_handle(h) {}
    DBTask(DBTask&& other) noexcept : m_handle(std::exchange(other.m_handle, {})) {}
    ~DBTask() { if (m_handle) m_handle.destroy(); }

    DBTask(const DBTask&) = delete;
    DBTask& operator=(const DBTask&) = delete;

private:
    std::coroutine_handle<promise_type> m_handle;
};


// void 특수화
template<>
class DBTask<void>
{
public:
    struct promise_type
    {
        std::exception_ptr      exception;
        std::coroutine_handle<> continuation;

        DBTask get_return_object()
        {
            return DBTask{ std::coroutine_handle<promise_type>::from_promise(*this) };
        }

        std::suspend_never initial_suspend() noexcept { return {}; }

        struct FinalAwaiter
        {
            bool await_ready() noexcept { return false; }
            void await_resume() noexcept {}
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept
            {
                auto cont = h.promise().continuation;
                return cont ? cont : std::noop_coroutine();
            }
        };
        FinalAwaiter final_suspend() noexcept { return {}; }

        void return_void() {}
        void unhandled_exception() { exception = std::current_exception(); }
    };

    bool await_ready() const noexcept { return m_handle && m_handle.done(); }

    void await_suspend(std::coroutine_handle<> caller) noexcept
    {
        m_handle.promise().continuation = caller;
    }

    void await_resume()
    {
        if (m_handle.promise().exception)
            std::rethrow_exception(m_handle.promise().exception);
    }

    explicit DBTask(std::coroutine_handle<promise_type> h) : m_handle(h) {}
    DBTask(DBTask&& other) noexcept : m_handle(std::exchange(other.m_handle, {})) {}
    ~DBTask() { if (m_handle) m_handle.destroy(); }

    DBTask(const DBTask&) = delete;
    DBTask& operator=(const DBTask&) = delete;

private:
    std::coroutine_handle<promise_type> m_handle;
};


// ─────────────────────────────────────────────────────────────────────────────
// DBResultAwaitable
//
// AsyncDBQueue::ExecuteAsync()가 내부적으로 리턴하는 awaitable.
// co_await 시 DB worker 스레드에 요청을 등록하고 suspend.
// DB worker가 완료되면 executor(스레드 풀)를 통해 코루틴을 resume.
//
// [수명 설계]
// await_suspend()가 끝나면 DBResultAwaitable 임시 객체는 소멸된다.
// 따라서 DB worker 콜백이 나중에 실행될 때 this를 참조하면 use-after-free.
// 이를 피하기 위해 결과를 담을 DBResult를 shared_ptr로 만들어
// awaitable과 콜백 람다가 함께 소유하도록 한다.
// await_resume()은 코루틴 프레임(살아있음)에서 shared_ptr을 통해 결과를 읽는다.
// ─────────────────────────────────────────────────────────────────────────────
class DBResultAwaitable
{
public:
    DBResultAwaitable(
        std::string           query,
        std::vector<DBParam>  params,
        std::function<void(std::string, std::vector<DBParam>, std::function<void(DBResult)>)> executeFn,
        IResumeExecutor*      pExecutor)
        : m_query(std::move(query))
        , m_params(std::move(params))
        , m_executeFn(std::move(executeFn))
        , m_pExecutor(pExecutor)
        , m_spResult(std::make_shared<DBResult>())  // 콜백과 공유할 결과 버퍼
    {}

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> handle)
    {
        // shared_ptr로 캡처 → awaitable 소멸 후에도 콜백이 안전하게 결과를 씀
        auto spResult   = m_spResult;
        auto pExecutor  = m_pExecutor;

        m_executeFn(std::move(m_query), std::move(m_params),
            [spResult, pExecutor, handle](DBResult result) mutable
            {
                *spResult = std::move(result);

                if (pExecutor)
                    pExecutor->Post([handle]() mutable { handle.resume(); });
                else
                    handle.resume();
            });
    }

    DBResult await_resume() { return std::move(*m_spResult); }

private:
    std::string           m_query;
    std::vector<DBParam>  m_params;
    std::function<void(std::string, std::vector<DBParam>, std::function<void(DBResult)>)> m_executeFn;
    IResumeExecutor*      m_pExecutor = nullptr;
    std::shared_ptr<DBResult> m_spResult;
};

} // namespace db
