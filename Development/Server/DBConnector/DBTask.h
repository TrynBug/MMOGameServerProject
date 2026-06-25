#pragma once

#include "pch.h"
#include "DBTypes.h"

#include <cassert>

namespace db
{

// ─────────────────────────────────────────────────────────────────────────────
// IResumeExecutor
//
// co_await 완료 후 코루틴을 어느 스레드에서 재개할지 결정하는 인터페이스.
//
// 사용 예:
//   - 실제 서버 : ServerBase의 IOCP Worker 스레드(GetCoroutineResumeExecutor) 또는
//                 Stage 컨텐츠 스레드(Stage::GetResumeExecutor)를 연결.
//
// [필수] 서버에서는 반드시 non-null executor를 넘겨야 한다. nullptr이면 DBResultAwaitable이
// DB 작업을 시작하지 않고 즉시 실패 처리한다(worker 스레드 직접 resume = 스레드 안정성 위반).
// ─────────────────────────────────────────────────────────────────────────────
class IResumeExecutor
{
public:
    virtual ~IResumeExecutor() = default;
    virtual void Post(std::function<void()> fn) = 0;
};


// ─────────────────────────────────────────────────────────────────────────────
// AwaitableCoTask<T>
//
// AsyncDBQueue::ExecuteAsync()가 리턴하는 코루틴 타입.
// 반드시 co_await으로 호출해야 하며 DBResult를 기다릴 수 있다.
//
// 사용 예:
//   db::AwaitableCoTask<db::DBResult> MyCoroutine(db::AsyncDBQueue& dbQueue)
//   {
//       db::DBResult r1 = co_await dbQueue.ExecuteAsync("SELECT ...", {});
//       db::DBResult r2 = co_await dbQueue.ExecuteAsync("UPDATE ...", {r1.GetInt64(0, "id")});
//       co_return r2;
//   }
// ─────────────────────────────────────────────────────────────────────────────
template<typename T = void>
class AwaitableCoTask
{
public:
    // ── promise_type ────────────────────────────────────────────────────────
    struct promise_type
    {
        std::optional<T>         result;
        std::exception_ptr       exception;
        std::coroutine_handle<>  continuation; // 이 태스크를 co_await 하는 바깥 코루틴

        AwaitableCoTask get_return_object()
        {
            return AwaitableCoTask{ std::coroutine_handle<promise_type>::from_promise(*this) };
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

    // ── awaitable (co_await AwaitableCoTask<T> 할 때 사용) ──────────────────────────
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
    explicit AwaitableCoTask(std::coroutine_handle<promise_type> h) : m_handle(h) {}
    AwaitableCoTask(AwaitableCoTask&& other) noexcept : m_handle(std::exchange(other.m_handle, {})) {}
    ~AwaitableCoTask() { if (m_handle) m_handle.destroy(); }

    AwaitableCoTask(const AwaitableCoTask&) = delete;
    AwaitableCoTask& operator=(const AwaitableCoTask&) = delete;

private:
    std::coroutine_handle<promise_type> m_handle;
};


// void 특수화
template<>
class AwaitableCoTask<void>
{
public:
    struct promise_type
    {
        std::exception_ptr      exception;
        std::coroutine_handle<> continuation;

        AwaitableCoTask get_return_object()
        {
            return AwaitableCoTask{ std::coroutine_handle<promise_type>::from_promise(*this) };
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

    explicit AwaitableCoTask(std::coroutine_handle<promise_type> h) : m_handle(h) {}
    AwaitableCoTask(AwaitableCoTask&& other) noexcept : m_handle(std::exchange(other.m_handle, {})) {}
    ~AwaitableCoTask() { if (m_handle) m_handle.destroy(); }

    AwaitableCoTask(const AwaitableCoTask&) = delete;
    AwaitableCoTask& operator=(const AwaitableCoTask&) = delete;

private:
    std::coroutine_handle<promise_type> m_handle;
};


// ─────────────────────────────────────────────────────────────────────────────
// DetachedCoTask
//
// fire-and-forget 코루틴 진입점용 태스크 타입.
// 호출자가 반환값을 보관할 필요가 없으며, 코루틴이 종료되면 자기 자신을 파괴한다.
//
// AwaitableCoTask<T>는 다른 코루틴에서 co_await으로 결과를 받는 용도이므로
// 호출자가 핸들을 보관해야 한다. 반면 패킷 핸들러 같은 최상위 진입점은
// 반환값을 받아 보관할 곳이 없기 때문에 AwaitableCoTask<void>를 그대로 쓰면
// 호출 즉시 AwaitableCoTask 임시 객체가 소멸하면서 코루틴 프레임이 destroy 되어버린다.
// 이후 DB 콜백이 핸들을 resume하면 use-after-free.
//
// DetachedCoTask는 final_suspend에서 suspend_never를 리턴하여 코루틴 종료 시
// 컴파일러가 자동으로 프레임을 파괴하도록 한다. get_return_object는 비어있는
// 객체를 리턴하므로 호출자가 반환값을 그대로 버려도 안전하다.
//
// 사용 예:
//   db::DetachedCoTask MyHandler(SessionPtr s, Packet p)
//   {
//       db::DBResult r = co_await dbQueue.ExecuteAsync(...);
//       // ... 처리 ...
//       co_return;
//   }
//
// [주의] 코루틴 본문에서 던진 예외는 외부로 전파할 방법이 없으므로
// unhandled_exception에서 디버그 출력만 하고 swallow 한다.
// 사용자가 정교한 예외 처리를 원한다면 코루틴 본문에서 try/catch 해야 한다.
// ─────────────────────────────────────────────────────────────────────────────
struct DetachedCoTask
{
    struct promise_type
    {
        DetachedCoTask get_return_object() noexcept { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept
        {
            try { std::rethrow_exception(std::current_exception()); }
            catch (const std::exception& e)
            {
                ::OutputDebugStringA("DetachedCoTask unhandled exception: ");
                ::OutputDebugStringA(e.what());
                ::OutputDebugStringA("\r\n");
            }
            catch (...)
            {
                ::OutputDebugStringA("DetachedCoTask unhandled unknown exception\r\n");
            }
        }
    };
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
    // starter(cb): 작업을 시작하고, 완료 시 cb(DBResult)를 호출하기로 약속하는 함수.
    //   - 단발 쿼리(ExecuteAsync)나 트랜잭션(TransactionAsync) 모두 자기 방식대로 starter를 만든다.
    //   - cb는 worker 스레드에서 호출된다.
    using Starter = std::function<void(std::function<void(DBResult)>)>;

    DBResultAwaitable(Starter starter, IResumeExecutor* pExecutor)
        : m_starter(std::move(starter))
        , m_pExecutor(pExecutor)
        , m_spResult(std::make_shared<DBResult>())  // 콜백과 공유할 결과 버퍼
    {}

    bool await_ready() const noexcept { return false; }

    // 반환값:
    //   true  → suspend(정상). worker 완료 후 executor가 코루틴을 재개.
    //   false → suspend 안 함. 호출 스레드에서 즉시 await_resume로 진행(실패 결과).
    bool await_suspend(std::coroutine_handle<> handle)
    {
        // [필수 불변식] 서버에서는 반드시 resume executor가 있어야 스레드 안정성이 보장된다.
        // executor가 없으면 worker 스레드에서 직접 resume하게 되는데, 그러면 게임상태를 엉뚱한
        // 스레드에서 만지게 된다(Stage 단일스레드 불변식 위반). 이런 호출은 버그이므로
        // DB 작업을 시작하지 않고(큐에 넣지 않고) 즉시 실패로 끝낸다.
        if (m_pExecutor == nullptr)
        {
            ::OutputDebugStringA("DBResultAwaitable: pExecutor is null - DB request aborted (resume executor is required)\r\n");
            assert(false && "DBResultAwaitable requires a resume executor (pExecutor)");
            m_spResult->success  = false;
            m_spResult->errorMsg = "DB request aborted: resume executor (pExecutor) is null";
            return false;   // suspend 안 함 → 같은 스레드에서 await_resume가 실패 결과를 돌려준다.
        }

        // shared_ptr로 캡처 → awaitable 소멸 후에도 콜백이 안전하게 결과를 씀
        auto spResult  = m_spResult;
        auto pExecutor = m_pExecutor;   // 위에서 non-null 보장

        m_starter(
            [spResult, pExecutor, handle](DBResult result) mutable
            {
                *spResult = std::move(result);
                pExecutor->Post([handle]() mutable { handle.resume(); });
            });
        return true;
    }

    DBResult await_resume() { return std::move(*m_spResult); }

private:
    Starter                   m_starter;
    IResumeExecutor*          m_pExecutor = nullptr;
    std::shared_ptr<DBResult> m_spResult;
};

} // namespace db
