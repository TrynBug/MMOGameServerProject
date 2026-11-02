#include "DB.h"
#include "Example.h"
#include <mysql.h>
#include <stdexcept>
#include <utility>

DbAwaitable::DbAwaitable(DbWorkerPool& pool, std::shared_ptr<const DbRequest> request,
    std::shared_ptr<ResumeExecutor> executor)
    : m_pool(pool), m_request(std::move(request)), m_executor(std::move(executor)), m_result(std::make_shared<DbResult>())
{
    Check(m_request && m_executor, "DB request and resume executor are required");
}

void DbAwaitable::await_suspend(std::coroutine_handle<> handle)
{
    // [#코루틴 08 | IOCP worker] 현재 코루틴은 이미 suspend된 상태이다. 
    // DB job을 등록한다.
    m_pool.Submit(
        m_request,  // 프로시저 입력 데이터.
        m_result,   // DB worker가 채울 결과 버퍼.
        m_executor, // 재개 작업을 실행할 IOCP executor.
        handle);    // 중단된 코루틴 프레임의 비소유 핸들.

    // 주의: job 등록 직후 다른 IOCP worker에서 재개/종료될 수 있으므로 이후 this에 접근하지 않는다.
}

DbWorkerPool::DbWorkerPool(std::shared_ptr<const DbConfig> config, int workerCount)
{
    // [#시작 02 | main] worker 수만큼 연결을 미리 연다. 이 루프에서는 스레드를 생성하지 않는다.
    for (int index = 0; index < workerCount; ++index)
    {
        std::shared_ptr<MYSQL> connection(mysql_init(nullptr), mysql_close);
        Check(connection != nullptr, "mysql_init failed");

        unsigned int timeout = 5;
        mysql_options(connection.get(), MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
        mysql_options(connection.get(), MYSQL_OPT_READ_TIMEOUT, &timeout);
        mysql_options(connection.get(), MYSQL_OPT_WRITE_TIMEOUT, &timeout);

        if (mysql_real_connect(connection.get(), config->host.c_str(), config->user.c_str(),
            config->password.c_str(), "CoroutineExample", config->port, nullptr, CLIENT_MULTI_RESULTS) == nullptr)
        {
            // 생성자 실패 시 지역 connection과 이미 저장한 연결들이 자동으로 닫힌다.
            // worker는 아직 시작하지 않았으므로 DB 요청을 받을 수 없다.
            throw std::runtime_error("DB connection " + std::to_string(index) + " initialization failed: " + mysql_error(connection.get()));
        }
        m_connections.push_back(std::move(connection));
    }

    // [#시작 03 | main] 모든 연결이 성공했을 때만 worker를 시작한다. 연결은 스레드별로 고정 배정한다.
    try
    {
        for (int index = 0; index < workerCount; ++index)
            m_workers.emplace_back(&DbWorkerPool::run, this, m_connections[index].get());
    }
    catch (...)
    {
        // 스레드 생성 도중 실패해도 이미 시작한 worker를 종료한 뒤 예외를 전달한다.
        Stop();
        throw;
    }
}

DbWorkerPool::~DbWorkerPool()
{
    Stop();
}

DbAwaitable DbWorkerPool::Execute(std::shared_ptr<const DbRequest> request, std::shared_ptr<ResumeExecutor> executor)
{
    // [#코루틴 06 | IOCP worker] awaitable 객체(DbAwaitable 객체)를 생성한다.
    // DbAwaitable 에는 request, executor를 저장해둔다. 그리고 DbAwaitable 에는 DB 결과를 담을 멤버변수(m_result)도 준비되어 있다.
    return DbAwaitable(*this, std::move(request), std::move(executor));
}

void DbWorkerPool::Submit(std::shared_ptr<const DbRequest> request, std::shared_ptr<DbResult> result,
    std::shared_ptr<ResumeExecutor> executor, std::coroutine_handle<> handle)
{
    // [#코루틴 09 | IOCP worker 스레드] DB 작업 요청을 큐에 넣고 DB worker 스레드를 깨운다.
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
    // 안전하게 종료하려면 호출자가 새 job 등록을 멈추고 진행 중인 코루틴 완료를 먼저 보장해야 한다.
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
    // 모든 worker가 연결 사용을 끝냈으므로 main에서 전용 연결들을 닫는다.
    m_connections.clear();
}


// DB worker 스레드 루틴
void DbWorkerPool::run(MYSQL* connection)
{
    Check(mysql_thread_init() == 0, "mysql_thread_init failed");

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

            // [#코루틴 11 | DB worker 스레드] 큐에서 DB Job을 꺼낸다.
            job = std::move(m_jobs.front());
            m_jobs.pop();
        }

        Trace("DB worker: synchronous CALL");

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

        // [#코루틴 12 | DB worker 스레드] DB 프로시저를 동기로 실행하고 결과를 job.result에 채운다.
		// 여기서 job.result 는 [#코루틴 08] 에서 전달된 std::shared_ptr<DbResult> 이다.
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
                    const auto lengths = mysql_fetch_lengths(rows);
                    for (unsigned int column = 0; column < mysql_num_fields(rows); ++column)
                    {
                        if (row[column])
                        {
                            values.emplace_back(std::string(row[column], lengths[column]));
                        }
                        else
                        {   
                            // SQL NULL은 문자열 "NULL"로 저장
                            values.emplace_back("NULL");
                        }
                        
                    }

                    // job.result 에 DB 결과를 채운다.
                    job.result->rows.push_back(std::move(values));
                }

                mysql_free_result(rows);
            }
            // UPDATE, INSERT만 있는 프로시저 등은 결과셋이 없다. 결과셋이 있어야 하는데 없을 때만 오류다.
            else if (mysql_field_count(connection) != 0)
            {
                status = 1;
                break;
            }

            // mysql_next_result: 0=다음 결과셋 있음, -1=모두 소비 완료, 양수값=오류코드
            status = mysql_next_result(connection);
        }

        if (status == -1)
        {
            job.result->success = true;
        }
        else
        {
            job.result->success = false;
            job.result->errorCode = mysql_errno(connection);
            job.result->errorMessage = mysql_error(connection);
            job.result->rows.clear();
        }

        // [#코루틴 13 | DB worker 스레드] 코루틴 재개 작업을 만든다.
        // 여기서 job.handle은 [#코루틴 08] 에서 전달된 std::coroutine_handle 이다.
        // handle.resume() 을 호출하면 코루틴이 재개된다.
		// 주의: handle.resume() 호출 이후에는 job.handle을 사용하지 않는다. 코루틴이 재개되면 프레임이 파괴될 수 있기 때문이다.
        auto resume = [handle = job.handle] 
            { 
                // [#코루틴 13-1 | IOCP worker 스레드] IOCP worker 스레드에서 이 람다함수를 호출하였다.
                // handle.resume()은 코루틴을 재개한다. 
                // handle.resume() 함수 안에서는 handle이 가리키는 코루틴 프레임의 awaitable.await_resume 함수를 호출한다.
                // 그리고 suspend 되었던 지점에서 실행을 이어간다.
                handle.resume(); 
            };

        // [#코루틴 14 | DB worker 스레드] 코루틴 재개 작업을 job.executor를 통해 IOCP 완료통지 큐에 등록한다. 
        // 여기서 job.executor 는 [#코루틴 08] 에서 전달된 std::shared_ptr<IocpResumeExecutor> 이다.
        // IocpResumeExecutor 는 코루틴이 어느 스레드에서 재개되어야 하는지를 결정하는 역할을 한다(여기서는 IOCP worker 스레드).
        // C++ 코루틴은 코루틴이 suspend된 스레드에서 재개되는것을 보장해주지 않는다. 사용자가 직접 그렇게 만들어야 한다.
        job.executor->Post(std::move(resume));
    }


    mysql_thread_end();
}
