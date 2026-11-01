#include "Example.h"
#include <mysql.h>
#include <iostream>
#include <stdexcept>
#include <syncstream>

void Trace(const std::string& message)
{
    // 여러 worker의 로그가 한 줄 안에서 섞이지 않도록 출력 단위를 묶는다.
    std::osyncstream(std::cout) << "[thread " << GetCurrentThreadId() << "] " << message << '\n';
}

void Check(bool condition, const char* message)
{
    // 실행 전제나 스레드 소유 규칙이 깨졌을 때 C++ 예외로 알린다.
    if (!condition)
        throw std::runtime_error(message);
}

void QueueExecutor::Post(std::function<void()> task)
{
    // task와 그 캡처를 큐로 넘긴다. 생산자와 소비자는 같은 mutex로 큐에 접근한다.
    {
        std::lock_guard lock(m_mutex);
        Check(!m_stopped, "Post after executor shutdown");
        m_tasks.push(std::move(task));
    }
    // lock을 놓은 뒤 알린다. 실제 작업 실행은 소비자의 RunOne에서 일어난다.
    m_ready.notify_one();
}

bool QueueExecutor::RunOne()
{
    std::function<void()> task;
    {
        std::unique_lock lock(m_mutex);
        // wait는 대기 중 mutex를 해제하며, 깨어나면 다시 획득하여 조건을 검사한다.
        // predicate가 있으므로 알림이 먼저 도착하거나 불필요하게 깨어나도 안전하다.
        m_ready.wait(lock, [this] { return m_stopped || !m_tasks.empty(); });
        if (m_tasks.empty())
            return false;
        task = std::move(m_tasks.front());
        m_tasks.pop();
    }
    // [#코루틴 13 | main] 첫 RunOne은 DB worker가 넣은 재개 작업을 꺼내 handle.resume()를 실행한다.
    // Initialize는 await_resume(14)부터 이어진다. 큐 lock은 해제된 상태다.
    task();
    // [#코루틴 19 | main] Initialize 종료로 resume 작업이 반환된다. finished는 아직 0이다.
    // 이 RunOne이 반환되면 main은 루프를 다시 돌며 완료 통지를 처리한다(20).
    // 두 번째 RunOne에서는 task()가 완료 통지이므로 여기서 반환한 뒤 main의 21로 진행한다.
    return true;
}

void QueueExecutor::Run()
{
    while (RunOne()) {}
}

void QueueExecutor::Stop()
{
    // 이미 큐에 있는 작업은 RunOne에서 끝까지 처리하고, 큐가 비면 루프를 종료한다.
    // Stop 뒤 새 Post는 금지하므로 DB 완료 통지가 더 오지 않는 시점에 호출해야 한다.
    {
        std::lock_guard lock(m_mutex);
        m_stopped = true;
    }
    m_ready.notify_all();
}

IocpExecutor::IocpExecutor(int workerCount)
{
    // INVALID_HANDLE_VALUE로 소켓 연결 없이 completion port만 만든다.
    m_port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, workerCount);
    Check(m_port != nullptr, "CreateIoCompletionPort failed");
    for (int index = 0; index < workerCount; ++index)
        m_workers.emplace_back(&IocpExecutor::run, this);
}

IocpExecutor::~IocpExecutor()
{
    Stop();
}

void IocpExecutor::Stop()
{
    if (m_port == nullptr)
        return;
    // 호출자는 모든 코루틴의 완료를 확인한 뒤 executor를 파괴한다.
    // key=0은 일반 작업 포인터와 구분하는 종료 신호다. worker 수만큼 보내 모두 깨운다.
    for (size_t index = 0; index < m_workers.size(); ++index)
        PostQueuedCompletionStatus(m_port, 0, 0, nullptr);
    for (auto& worker : m_workers)
        worker.join();
    CloseHandle(m_port);
    m_port = nullptr;
}

void IocpExecutor::Post(std::function<void()> task)
{
    // Windows 큐는 std::function을 보관하지 않으므로 힙 객체의 주소를 completion key로 전달한다.
    // 등록 실패 시 unique_ptr가 정리하고, 성공하면 수신 worker에게 정리 책임을 넘긴다.
    auto work = std::make_unique<std::function<void()>>(std::move(task));
    Check(PostQueuedCompletionStatus(m_port, 0, reinterpret_cast<ULONG_PTR>(work.get()), nullptr) != FALSE,
        "PostQueuedCompletionStatus failed");
    work.release();
}

void IocpExecutor::run()
{
    while (true)
    {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        OVERLAPPED* overlapped = nullptr;
        Check(GetQueuedCompletionStatus(m_port, &bytes, &key, &overlapped, INFINITE) != FALSE,
            "GetQueuedCompletionStatus failed");
        if (key == 0)
            return;
        // Post에서 넘긴 소유권을 회수한다. 실행 후 이 지역 unique_ptr가 작업 객체를 삭제한다.
        std::unique_ptr<std::function<void()>> work(reinterpret_cast<std::function<void()>*>(key));
        (*work)();
    }
}

void DetachedTask::promise_type::unhandled_exception() noexcept
{
    // 코루틴 본문 밖으로 나온 C++ 예외는 프로세스 종료로 처리한다.
    // MySQL 오류는 DbResult에 담기므로 이 함수로 전달되지 않는다.
    std::terminate();
}

DbWorkerPool::DbWorkerPool(std::shared_ptr<const DbConfig> config, int workerCount)
{
    for (int index = 0; index < workerCount; ++index)
        m_workers.emplace_back(&DbWorkerPool::run, this, config);
}

DbWorkerPool::~DbWorkerPool()
{
    Stop();
}

DbAwaitable DbWorkerPool::Execute(std::shared_ptr<const DbRequest> request, std::shared_ptr<ResumeExecutor> executor)
{
    // [#코루틴 05 | main 스레드] awaitable 객체(DbAwaitable 객체)를 생성한다.
    // DbAwaitable 에는 request, executor를 저장해둔다. 그리고 DbAwaitable 에는 DB 결과를 담을 멤버변수(m_result)도 준비되어 있다.
    return DbAwaitable(*this, std::move(request), std::move(executor));
}

void DbWorkerPool::Submit(std::shared_ptr<const DbRequest> request, std::shared_ptr<DbResult> result,
    std::shared_ptr<ResumeExecutor> executor, std::coroutine_handle<> handle)
{
    // [#코루틴 08 | main 스레드] DB 작업 요청을 큐에 넣고 DB worker 스레드를 깨운다.
    {
        std::lock_guard lock(m_mutex);
        Check(!m_stopped, "DB request after shutdown");

        // DB job을 큐에 넣는다.
        m_jobs.push(Job{ 
            std::move(request),   // DB 요청 데이터
			std::move(result),    // DB 처리결과를 저장할 버퍼
			std::move(executor),  // 코루틴 재개 작업을 등록할 큐
            handle });            // 코루틴 프레임의 핸들
    }

    // DO worker 스레드 깨우기
    m_ready.notify_one();
}

void DbWorkerPool::Stop()
{
    // 신규 Submit을 막고 남은 큐를 처리한 뒤 worker를 join한다.
    // worker 종료는 resume 작업의 등록 완료를 뜻할 뿐, executor의 후속 처리 완료까지 보장하지 않는다.
    // main은 먼저 모든 최상위 절차의 완료를 확인하고 나서 이 함수를 호출한다.
    {
        std::lock_guard lock(m_mutex);
        m_stopped = true;
    }
    m_ready.notify_all();
    for (auto& worker : m_workers)
    {
        if (worker.joinable())
            worker.join();
    }
}


// DB worker 스레드 루틴
void DbWorkerPool::run(std::shared_ptr<const DbConfig> config)
{
    Check(mysql_thread_init() == 0, "mysql_thread_init failed");

    // MySQL 연결 생성 및 초기화
    MYSQL* connection = mysql_init(nullptr);
    Check(connection != nullptr, "mysql_init failed");

    unsigned int timeout = 5;
    mysql_options(connection, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    mysql_options(connection, MYSQL_OPT_READ_TIMEOUT, &timeout);
    mysql_options(connection, MYSQL_OPT_WRITE_TIMEOUT, &timeout);

    bool connected = false;
    if (mysql_real_connect(connection, 
        config->host.c_str(), 
        config->user.c_str(),
        config->password.c_str(), 
        "CoroutineExample", 
        config->port, 
        nullptr, 
        CLIENT_MULTI_RESULTS) != nullptr)
    {
        connected = true;
    }

    Check(connected == true, "mysql_real_connect failed");
    return;

    // loop
    while (true)
    {
        Job job;
        {
            std::unique_lock lock(m_mutex);

            // wait
            m_ready.wait(lock, [this] { return m_stopped || !m_jobs.empty(); });

            if (m_jobs.empty())
                break;

            // [#코루틴 10 | DB worker 스레드] 큐에서 DB Job을 꺼낸다.
            job = std::move(m_jobs.front());
            m_jobs.pop();
        }

        Trace("DB worker: synchronous CALL");
        DbResult& result = *job.result;

        // job 종류에 맞는 프로시저 호출문을 만든다.
        std::string sql;
        switch (job.request->operation)
        {
        case DbOperation::Read:
            sql = "CALL ReadGold(" + std::to_string(job.request->playerId) + ")";
            break;
        case DbOperation::Write:
            sql = "CALL WriteGold(" + std::to_string(job.request->playerId) + "," + std::to_string(job.request->gold) + ")";
            break;
        case DbOperation::Fail:
            sql = "CALL FailExample()";
            break;
        }

        // [#코루틴 11 | DB worker] CALL ReadGold(1)을 동기 실행하고 아래 루프에서 결과를 job.result에 채운다.
        // 연결 실패 시에는 위의 !connected 분기에서 오류 결과를 채우고 12로 진행한다.
        int status = mysql_query(connection, sql.c_str());
        while (status == 0)
        {
            MYSQL_RES* rows = mysql_store_result(connection);
            if (rows != nullptr)
            {
                while (MYSQL_ROW row = mysql_fetch_row(rows))
                {
                    std::vector<std::string> values;
                    // MySQL 결과 버퍼가 해제되어도 결과를 읽을 수 있도록 std::string으로 복사한다.
                    // SQL NULL은 문자열 "NULL"로 저장한다.
                    const auto lengths = mysql_fetch_lengths(rows);
                    for (unsigned int column = 0; column < mysql_num_fields(rows); ++column)
                        values.emplace_back(row[column] ? std::string(row[column], lengths[column]) : "NULL");
                    result.rows.push_back(std::move(values));
                }
                mysql_free_result(rows);
            }
            // 결과셋이 없는 UPDATE/OK도 정상이다. 컬럼이 있어야 하는데 결과가 없을 때만 오류다.
            else if (mysql_field_count(connection) != 0)
            {
                status = 1;
                break;
            }
            // CALL의 마지막 OK를 포함한 모든 결과를 소비해야 다음 CALL이 가능하다.
            status = mysql_next_result(connection);
        }
        // mysql_next_result: 0=다음 결과 있음, -1=모두 소비 완료, 양수=오류.
        result.success = status == -1;
        if (!result.success)
        {
            result.errorCode = mysql_errno(connection);
            result.errorMessage = mysql_error(connection);
            result.rows.clear();
        }
        

        // [#코루틴 12 | DB worker] QueueExecutor::Post로 main 큐에 handle.resume() 작업을 등록한다.
        // main의 RunOne이 이 작업을 꺼내 실행한다(13). DB worker는 다음 DB 요청을 처리한다.
        // [프레임 상태: 재개 작업을 등록하기 직전]
        // 요청 코루틴의 인자/지역변수/awaiter는 중단된 채 유지되어 있다.
        // worker가 변경한 것은 별도 DbResult 객체다. 코루틴의 result 지역변수에 직접 대입한 것이 아니다.
        // executor 큐의 람다는 handle만 보유하며, 재개되면 await_resume가 그 결과를 호출부로 돌려준다.
        // resume는 await_resume부터 후속 코드를 실행하고, 다음 중단 또는 본문 종료 시 반환한다.
        // 본문이 끝나면 프레임이 파괴될 수 있으므로 람다는 resume 이후 handle을 사용하지 않는다.
        job.executor->Post([handle = job.handle] { handle.resume(); });
    }
    mysql_close(connection);
    mysql_thread_end();
}

Contents::Contents() : m_owner(std::this_thread::get_id()) {}

void Contents::RequestRemove()
{
    // 지금 삭제할 수 없어도 제거 의도는 기억한다. 마지막 AsyncPin이 해제될 때 재검사한다.
    CheckOwner();
    removeRequested = true;
    if (inFlightAsync == 0)
    {
        character.reset();
        removed = true;
        Trace("Contents: removal completed");
    }
    else
        Trace("Contents: removal deferred by AsyncPin");
}

bool Contents::TryMove()
{
    // 제거되지 않았고 캐릭터의 비동기 작업이 없을 때 이동을 허용한다.
    CheckOwner();
    return !removed && character->pendingAsync == 0;
}

void Contents::CheckOwner() const
{
    Check(m_owner == std::this_thread::get_id(), "Contents resumed on wrong thread");
}

AsyncPin::AsyncPin(std::shared_ptr<Contents> contents)
    : m_contents(std::move(contents)), m_character(m_contents->character)
{
    // 동일한 소유 스레드에서 카운터를 올려 제거/이동을 차단한다.
    m_contents->CheckOwner();
    Check(!m_contents->removed, "Cannot pin removed contents");
    ++m_contents->inFlightAsync;
    ++m_character->pendingAsync;
}

AsyncPin::~AsyncPin()
{
    // 보호 범위를 벗어나면 작업 수를 줄인다. 제거가 예약되어 있으면 남은 작업 수를 확인하여 처리한다.
    // co_await 대기 중에는 pin이 유지되므로 이 소멸자는 실행되지 않는다.
    m_contents->CheckOwner();
    --m_character->pendingAsync;
    --m_contents->inFlightAsync;
    if (m_contents->removeRequested)
        m_contents->RequestRemove();
}
