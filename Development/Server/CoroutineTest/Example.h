#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <condition_variable>
#include <coroutine>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

void Trace(const std::string& message);
void Check(bool condition, const char* message);

// 코루틴의 후속 코드를 실행할 스레드에 작업을 전달하는 인터페이스다.
// DB worker는 Post로 재개 작업을 등록하고, 대상 스레드가 나중에 그 작업을 실행한다.
class ResumeExecutor
{
public:
    virtual ~ResumeExecutor() = default;
    virtual void Post(std::function<void()> task) = 0;
};

// main 또는 컨텐츠 스레드가 실행할 함수들을 보관하는 스레드 안전 큐다.
// main은 RunOne으로 하나씩 처리하고, 컨텐츠 스레드는 Run으로 반복 처리한다.
// 각 큐는 하나의 소비 스레드에 배정하며 스레드 생성은 main에서 수행한다.
class QueueExecutor : public ResumeExecutor
{
public:
    void Post(std::function<void()> task) override;
    // 작업이 올 때까지 기다려 하나를 실행한다. 종료 요청 후 큐가 비면 false를 반환한다.
    bool RunOne();
    // 종료 요청 후 남은 작업을 모두 처리할 때까지 RunOne을 반복한다.
    void Run();
    // 새 작업 등록을 막고 대기 중인 소비 스레드를 깨운다.
    void Stop();
private:
    std::mutex m_mutex;
    std::condition_variable m_ready;
    std::queue<std::function<void()>> m_tasks;
    bool m_stopped = false;
};

// IOCP 포트 하나와 그 포트에서 작업을 꺼내 실행하는 worker들을 소유한다.
// 사용자 completion packet에 함수 포인터를 실어 전달하며, worker 중 하나가 함수를 실행한다.
class IocpExecutor : public ResumeExecutor
{
public:
    explicit IocpExecutor(int workerCount);
    ~IocpExecutor();
    void Post(std::function<void()> task) override;
    void Stop();
private:
    void run();
    HANDLE m_port = nullptr;
    std::vector<std::thread> m_workers;
};

// 호출 즉시 실행을 시작하고 본문 종료 시 프레임을 자동 해제하는 코루틴의 반환 타입이다.
// promise_type은 컴파일러가 사용하는 시작/종료/예외 처리 규칙을 정의한다.
// 실행 위치와 필요한 인자/지역변수는 별도 코루틴 프레임에 유지되며, DB Job의 handle로 재개한다.
struct DetachedTask
{
    struct promise_type
    {
        // [#코루틴 02 | main 스레드] 호출자에게 반환할 빈 DetachedTask 객체를 만든다.
        DetachedTask get_return_object() noexcept 
        { 
            return {}; 
        }

        // [#코루틴 03 | main 스레드] suspend_never이므로 중단하지 않고 Initialize 본문으로 들어간다(04).
        std::suspend_never initial_suspend() noexcept 
        { 
            return {}; 
        }

        // [#코루틴 18 | main 스레드] 마지막 중단 없이 promise/인자를 정리하고 프레임을 해제한다.
        // handle.resume()가 반환되면 QueueExecutor::RunOne의 task() 실행도 끝난다(19).
        std::suspend_never final_suspend() noexcept { return {}; }
        // [#코루틴 17 | main] Initialize 본문 끝에 도달하여 반환 처리를 한다. 반환 데이터는 없다.
        void return_void() noexcept {}
        void unhandled_exception() noexcept;
    };
};

// 각 DB worker가 MySQL에 접속할 때 사용하는 공통 설정이다.
struct DbConfig
{
    std::string host;
    std::string user;
    std::string password;
    unsigned int port = 3306;
};

// Read=gold 조회, Write=gold 저장, Fail=오류 결과 반환용 프로시저 호출.
enum class DbOperation { Read, Write, Fail };

// 프로시저 종류와 입력값을 묶은 DB 요청 데이터다. gold는 Write에서만 사용한다.
// 호출부와 DB Job이 shared_ptr<const DbRequest>로 공유하며 처리 중에는 값을 변경하지 않는다.
struct DbRequest
{
    DbOperation operation = DbOperation::Read;
    int playerId = 1;
    int gold = 0;
};

// 프로시저 실행의 성공 여부, 오류 정보, 조회 행을 담는 공통 결과 데이터다.
// DB worker가 내용을 채운 뒤 executor를 통해 전달하면 재개된 코루틴이 읽는다.
struct DbResult
{
    bool success = false;
    unsigned int errorCode = 0;
    std::string errorMessage;
    // 바깥 vector는 행, 안쪽 vector는 각 행의 컬럼 값이다. Write 성공 시에는 비어 있다.
    std::vector<std::vector<std::string>> rows;
};

class DbWorkerPool;

// DB 요청을 등록하고 완료 결과를 co_await의 값으로 전달하는 대기 객체다.
// await_ready → await_suspend → 재개 → await_resume 순서로 호출된다.
class DbAwaitable
{
public:
    DbAwaitable(DbWorkerPool& pool, std::shared_ptr<const DbRequest> request, std::shared_ptr<ResumeExecutor> executor)
        : m_pool(pool), m_request(std::move(request)), m_executor(std::move(executor)), m_result(std::make_shared<DbResult>())
    {
        Check(m_request && m_executor, "DB request and resume executor are required");
    }

    // [#코루틴 06 | main 스레드] false를 반환하여 코루틴이 suspend 되도록 한다.
    bool await_ready() const noexcept 
    { 
        return false; 
    }

    // [#코루틴 07 | main 스레드] 컴파일러가 코루틴을 중단하고 코루틴 프레임의 handle을 넘겨준다.
    void await_suspend(std::coroutine_handle<> handle)
    {
        // 이 함수가 호출된 시점은 코루틴이 중단된 시점이다.

        // DB worker 스레드에 DB 요청을 전달한다.
        m_pool.Submit(
            m_request,   // DB 요청 데이터이다.
            m_result,    // DB 처리결과를 저장할 버퍼이다. DB worker 스레드가 DB 작업을 완료한 다음 여기에다가 결과를 채워준다.
			m_executor,  // 코루틴 재개 작업을 등록할 큐이다. DB worker 스레드는 이 큐에 '코루틴을 재개한다'라는 작업을 넣어준다. 이 큐의 작업은 main 스레드가 꺼내어 처리한다.
            handle);     // 코루틴 프레임의 핸들이다. 핸들이 없으면 코루틴 프레임에 접근할 수 없으므로 핸들을 전달한다.

		// 이 함수가 반환되면 코루틴은 중단된 상태로 남아있게 된다. 나중에 누군가 handle.resume()을 호출하면 코루틴이 재개된다.
    }

    // [#코루틴 14 | main] 재개된 코루틴에 DB 결과를 반환한다. Initialize의 result로 전달된다(15).
    std::shared_ptr<DbResult> await_resume() 
    { 
        return m_result; 
    }

private:
    DbWorkerPool& m_pool; // 참조이므로 모든 코루틴이 끝날 때까지 main이 pool을 유지해야 한다.
    std::shared_ptr<const DbRequest> m_request;
    std::shared_ptr<ResumeExecutor> m_executor;
    std::shared_ptr<DbResult> m_result;
};

// 공용 요청 큐와 DB worker들을 관리한다. 각 worker는 전용 MySQL 연결로 프로시저를 동기 호출한다.
// 완료된 요청은 요청자가 지정한 executor에 재개 작업을 등록한다.
class DbWorkerPool
{
public:
    DbWorkerPool(std::shared_ptr<const DbConfig> config, int workerCount);
    ~DbWorkerPool();
    // 호출부가 co_await할 대기 객체를 만든다. 요청 등록은 await_suspend에서 이루어진다.
    DbAwaitable Execute(std::shared_ptr<const DbRequest> request, std::shared_ptr<ResumeExecutor> executor);
    // 하나의 Job으로 묶어 큐에 추가하고 대기 중인 DB worker를 깨운다.
    void Submit(std::shared_ptr<const DbRequest> request, std::shared_ptr<DbResult> result,
        std::shared_ptr<ResumeExecutor> executor, std::coroutine_handle<> handle);
    void Stop();
private:
    void run(std::shared_ptr<const DbConfig> config);
    struct Job
    {
        // 입력, 출력, 재개 위치, 재개할 코루틴을 한 작업으로 묶어 큐에 넘긴다.
        std::shared_ptr<const DbRequest> request;
        std::shared_ptr<DbResult> result;
        std::shared_ptr<ResumeExecutor> executor;
        // handle은 프레임의 소유권이나 참조 카운트를 제공하지 않는 비소유 핸들이다.
        // DB 완료 시 한 번 재개한다. 그때까지 대상 프레임은 유효해야 한다.
        std::coroutine_handle<> handle;
    };
    std::mutex m_mutex;
    std::condition_variable m_ready;
    std::queue<Job> m_jobs;
    bool m_stopped = false;
    std::vector<std::thread> m_workers;
};

struct Character
{
    // 이 캐릭터를 사용하는 비동기 작업 수. 0이 될 때까지 이동을 거절한다.
    int pendingAsync = 0;
};

// 캐릭터를 소유하고 제거/이동 가능 여부를 관리한다. 생성한 컨텐츠 스레드에서만 접근한다.
class Contents
{
public:
    Contents();
    void RequestRemove(); // 진행 중인 비동기 작업이 있으면 제거를 예약한다.
    bool TryMove(); // 캐릭터를 이동시킬 수 있는지 반환한다. 실제 이동은 수행하지 않는다.
    void CheckOwner() const; // 현재 스레드가 소유 스레드인지 검사한다.
    std::shared_ptr<Character> character = std::make_shared<Character>();
    int inFlightAsync = 0; // 이 컨텐츠를 사용하는 AsyncPin 수.
    bool removeRequested = false; // 제거 요청을 받은 상태.
    bool removed = false; // 캐릭터 소유권을 해제하고 논리적 제거를 마친 상태.
private:
    std::thread::id m_owner;
};

// 생성 시 컨텐츠/캐릭터의 비동기 작업 수를 증가시키고, 소멸 시 감소시키는 RAII 보호 객체다.
// shared_ptr로 메모리 수명을 유지하고 카운터로 제거/이동을 막아 DB 후속 처리의 접근을 보장한다.
// 획득과 해제가 같은 컨텐츠 스레드에서 이루어지므로 카운터에 atomic이 필요하지 않다.
class AsyncPin
{
public:
    explicit AsyncPin(std::shared_ptr<Contents> contents);
    ~AsyncPin();
    AsyncPin(const AsyncPin&) = delete;
    AsyncPin& operator=(const AsyncPin&) = delete;
private:
    std::shared_ptr<Contents> m_contents;
    std::shared_ptr<Character> m_character;
};
