#pragma once
#include "DB.h"
#include <condition_variable>
#include <coroutine>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

struct MYSQL;
class IResumeExecutor;

// main에서 worker 전용 연결들을 미리 생성할 때 사용하는 공통 설정이다.
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


// 이 타입은 코루틴 함수의 반환 타입으로 사용될 수 있다. 왜냐하면 아래 2가지 조건을 만족하기 때문이다:
// 1. promise_type 이 있다.
// 2. promise_type::get_return_object 함수로 코루틴 함수 반환 타입 객체를 생성할 수 있다.
// 이 타입은 반환값이 없으므로 promise_type::return_void() 를 구현한다.
struct DetachedTask
{
    struct promise_type
    {
        // [#코루틴 02 | IOCP worker 스레드] 호출자에게 반환할 빈 DetachedTask 객체를 만든다.
        DetachedTask get_return_object() noexcept
        {
            return {};
        }

        // [#코루틴 03 | IOCP worker 스레드] 반환값이 suspend_never이므로 코루틴을 중단하지 않고 코루틴함수 본문으로 들어간다.
        std::suspend_never initial_suspend() noexcept
        {
            return {};
        }

        // [#코루틴 23 | IOCP worker 스레드] 코루틴 함수의 반환값은 없다.
        void return_void() noexcept {}

        // [#코루틴 24 | IOCP worker 스레드] 마지막 suspend 없이 promise/함수인자를 정리하고 코루틴 프레임을 해제한다.
        std::suspend_never final_suspend() noexcept 
        { 
            return {}; 
        }
        
        // 코루틴 본문에서 발생한 처리되지 않은 예외가 있으면 이 함수가 호출됨
        void unhandled_exception() noexcept
        {
            std::terminate();
        }

    };
};

class DbWorkerPool;


// 이 클래스 타입은 Awaitable 이다. 이 타입을 반환하는 함수는 co_await 키워드로 호출할 수 있다.
// 이 클래스의 객체는 co_await 키워드로 호출될 때 await_ready, await_suspend, await_resume 세 가지 함수를 통해 코루틴의 중단과 재개를 제어한다.
// Awaitable이 되기 위한 조건은 await_ready, await_suspend, await_resume 3개 함수를 제공하는 것이다.
class DbAwaitable
{
public:
    DbAwaitable(DbWorkerPool& pool, std::shared_ptr<const DbRequest> request, std::shared_ptr<IResumeExecutor> executor);

    // [#코루틴 07 | IOCP worker 스레드] false를 반환하여 코루틴이 suspend 되도록 한다.
    bool await_ready() const noexcept
    {
        return false;
    }

    // [#코루틴 08 | IOCP worker 스레드] 컴파일러가 코루틴을 중단하고 코루틴 프레임의 handle을 넘겨준다.
    void await_suspend(std::coroutine_handle<> handle);

    // [#코루틴 19 | IOCP worker 스레드] DB 처리결과를 반환한다.
    std::shared_ptr<DbResult> await_resume() 
    {
        // m_result 에는 DB worker 스레드가 채워준 DB처리 결과값이 들어있다.
        return m_result; 
    }

private:
    DbWorkerPool& m_pool;
    std::shared_ptr<const DbRequest> m_request;
    std::shared_ptr<IResumeExecutor> m_executor;
    std::shared_ptr<DbResult> m_result;
};

// 공용 요청 큐와 DB worker들을 관리한다. 각 worker는 전용 MySQL 연결로 프로시저를 동기 호출한다.
// 완료된 요청은 요청자가 지정한 executor에 재개 작업을 등록한다.
class DbWorkerPool
{
public:
    // main에서 연결을 모두 연 뒤 worker를 시작한다. 연결 실패 시 예외로 초기화를 중단한다.
    DbWorkerPool(std::shared_ptr<const DbConfig> config, int workerCount);
    ~DbWorkerPool();
    // 호출부가 co_await할 대기 객체를 만든다. 요청 등록은 await_suspend에서 이루어진다.
    DbAwaitable Execute(std::shared_ptr<const DbRequest> request, std::shared_ptr<IResumeExecutor> executor);
    // 하나의 Job으로 묶어 큐에 추가하고 대기 중인 DB worker를 깨운다.
    void Submit(std::shared_ptr<const DbRequest> request, std::shared_ptr<DbResult> result,
        std::shared_ptr<IResumeExecutor> executor, std::coroutine_handle<> handle);
    void Stop();
private:
    void run(MYSQL* connection);
    struct Job
    {
        // 입력, 출력, 재개 위치, 재개할 코루틴을 한 작업으로 묶어 큐에 넘긴다.
        std::shared_ptr<const DbRequest> request;
        std::shared_ptr<DbResult> result;
        std::shared_ptr<IResumeExecutor> executor;
        // handle은 프레임의 소유권이나 참조 카운트를 제공하지 않는 비소유 핸들이다.
        // DB 완료 시 한 번 재개한다. 그때까지 대상 프레임은 유효해야 한다.
        std::coroutine_handle<> handle;
    };
    std::mutex m_mutex;
    std::condition_variable m_ready;
    std::queue<Job> m_jobs;
    bool m_stopped = false;
    // index번째 연결은 index번째 worker 전용이다. worker join 이후 main에서 해제한다.
    std::vector<std::shared_ptr<MYSQL>> m_connections;
    std::vector<std::thread> m_workers;
};
