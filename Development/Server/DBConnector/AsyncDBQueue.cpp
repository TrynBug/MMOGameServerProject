#include "pch.h"
#include "AsyncDBQueue.h"

#include <format>
#include <chrono>
#include <random>
#include <thread>

namespace db
{

// ─────────────────────────────────────────────────────────────────────────────
// DBTransaction
// ─────────────────────────────────────────────────────────────────────────────
DBResult DBTransaction::Execute(const std::string& query, const std::vector<DBParam>& params)
{
    DBResult result = m_conn.Execute(query, params);
    if (!result.success)
    {
        m_failed        = true;
        m_lastErrorCode = result.errorCode;
        m_lastErrorMsg  = result.errorMsg;
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// 트랜잭션 실행 (worker 스레드에서 동기로). 일시적 에러면 본문을 재실행한다.
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
    // 재시도 대상 = 타이밍이 원인이라 다시 하면 대개 성공하는 에러.
    //   1213 deadlock / 1205 lock wait timeout / 2006·2013 연결끊김.
    bool isTransient(unsigned int errorCode)
    {
        return errorCode == 1213 || errorCode == 1205 || errorCode == 2006 || errorCode == 2013;
    }
    bool isConnectionLost(unsigned int errorCode)
    {
        return errorCode == 2006 || errorCode == 2013;
    }

    void backoff(int attempt, const AsyncDBQueue::TxOptions& options)
    {
        // base*attempt + 지터[0,base]. 지터로 동시 재시도가 겹치는 것(thundering herd)을 흩는다.
        thread_local std::mt19937 randomEngine{ std::random_device{}() };

        int baseMs = 1;
        if (options.backoffBaseMs > 0)
        {
            baseMs = options.backoffBaseMs;
        }

        std::uniform_int_distribution<int> jitter(0, baseMs);
        const int delayMs = baseMs * attempt + jitter(randomEngine);
        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }

    DBResult runTransaction(DBConnection& connection, const DBConnectionConfig& config,
                            const AsyncDBQueue::TxBody& body, const AsyncDBQueue::TxOptions& options)
    {
        int maxAttempts = 1;
        if (options.maxAttempts > 0)
        {
            maxAttempts = options.maxAttempts;
        }

        for (int attempt = 1; attempt <= maxAttempts; ++attempt)
        {
            // 일시적 에러 후 재시도 직전, 다음 시도를 준비하는 공통 처리.
            //   연결끊김이면 재연결 시도, 그 외 일시적이면 백오프. 더 못 하면 false 반환.
            auto prepareRetry = [&](unsigned int errorCode) -> bool
            {
                if (!isTransient(errorCode) || attempt >= maxAttempts)
                {
                    return false;
                }
                if (isConnectionLost(errorCode))
                {
                    connection.Open(config);   // close + 재연결 (실패해도 다음 시도의 쿼리가 에러로 드러난다)
                }
                backoff(attempt, options);
                return true;
            };

            DBResult beginResult = connection.Begin();
            if (!beginResult.success)
            {
                if (prepareRetry(beginResult.errorCode))
                {
                    continue;
                }
                DBResult result;
                result.errorCode = beginResult.errorCode;
                result.errorMsg  = std::format("BEGIN failed: {}", beginResult.errorMsg);
                return result;
            }

            DBTransaction transaction(connection);
            bool commitIntent = false;
            try
            {
                commitIntent = body(transaction);
            }
            catch (const std::exception& exception)
            {
                connection.Rollback();
                DBResult result;
                result.errorMsg = std::format("transaction body threw: {}", exception.what());
                return result;   // 예외는 재시도하지 않는다(원인 불명).
            }
            catch (...)
            {
                connection.Rollback();
                DBResult result;
                result.errorMsg = "transaction body threw unknown exception";
                return result;
            }

            // 본문 안에서 쿼리가 실패했다면 절대 커밋하지 않는다.
            if (transaction.Failed())
            {
                connection.Rollback();
                if (prepareRetry(transaction.LastErrorCode()))
                {
                    continue;
                }
                DBResult result;
                result.errorCode = transaction.LastErrorCode();   // 결정적 에러(1062 등) → 즉시 실패
                result.errorMsg  = transaction.LastErrorMsg();
                return result;
            }

            // 본문이 롤백을 선택(비즈니스 중단) → 재시도 없이 실패 반환(errorCode=0).
            if (!commitIntent)
            {
                connection.Rollback();
                DBResult result;
                result.errorMsg = "aborted by transaction body";
                return result;
            }

            DBResult commitResult = connection.Commit();
            if (!commitResult.success)
            {
                connection.Rollback();   // best-effort
                if (prepareRetry(commitResult.errorCode))
                {
                    continue;
                }
                DBResult result;
                result.errorCode = commitResult.errorCode;
                result.errorMsg  = std::format("COMMIT failed: {}", commitResult.errorMsg);
                return result;
            }

            DBResult result;
            result.success = true;   // 커밋 완료
            return result;
        }

        DBResult result;
        result.errorMsg = "transaction max attempts exhausted";
        return result;
    }
} // anonymous namespace

AsyncDBQueue::~AsyncDBQueue()
{
    Close();
}

bool AsyncDBQueue::Open(const std::vector<OpenEntry>& entries, int numWorkers)
{
    if (m_bRunning)
    {
        return true;
    }

    if (entries.empty())
    {
        m_lastError = "no databases given";
        return false;
    }
    if (numWorkers < 1)
    {
        numWorkers = 1;
    }

    auto clearAll = [this]
    {
        m_accountDb.reset();
        m_gameDbs.clear();
    };

    // 각 DB의 DbState를 만들고 커넥션 numWorkers개를 미리 연다.
    // 워커 수 = numWorkers이고 워커는 요청당 커넥션 1개만 점유하므로, 한 DB로 모든 워커가
    // 동시에 몰려도 커넥션이 모자랄 수 없다(별도 cap/상한 불필요).
    for (const auto& entry : entries)
    {
        if (entry.type == EDBType::Game && entry.index < 0)
        {
            m_lastError = std::format("invalid game db index {}", entry.index);
            clearAll();
            return false;
        }

        bool duplicate = false;
        if (entry.type == EDBType::Account)
        {
            duplicate = m_accountDb.has_value();
        }
        else
        {
            duplicate = m_gameDbs.contains(entry.index);
        }
        if (duplicate)
        {
            m_lastError = std::format("duplicate db (type={} index={})", static_cast<int>(entry.type), entry.index);
            clearAll();
            return false;
        }

        DbState state;
        state.config = std::make_shared<const DBConnectionConfig>(entry.config);
        state.freeConns.reserve(numWorkers);
        for (int connectionIndex = 0; connectionIndex < numWorkers; ++connectionIndex)
        {
            auto connection = std::make_unique<DBConnection>();
            if (!connection->Open(entry.config))
            {
                m_lastError = std::format("db (type={} index={}) connect failed: {}",
                    static_cast<int>(entry.type), entry.index, connection->GetLastError());
                clearAll();
                return false;
            }
            state.freeConns.push_back(std::move(connection));
        }

        if (entry.type == EDBType::Account)
        {
            m_accountDb = std::move(state);
        }
        else
        {
            m_gameDbs.emplace(entry.index, std::move(state));
        }
    }

    m_bRunning = true;

    m_workers.reserve(numWorkers);
    for (int workerIndex = 0; workerIndex < numWorkers; ++workerIndex)
    {
        m_workers.emplace_back(&AsyncDBQueue::workerProc, this);
    }

    return true;
}

void AsyncDBQueue::Close()
{
    if (!m_bRunning.exchange(false))
    {
        return;
    }

    m_cv.notify_all();

    for (auto& thread : m_workers)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }

    m_workers.clear();
    m_accountDb.reset();
    m_gameDbs.clear();
    m_queue.clear();
}

AsyncDBQueue::DbState* AsyncDBQueue::getDbState(EDBType type, int index)
{
    if (type == EDBType::Account)
    {
        if (m_accountDb)
        {
            return &*m_accountDb;
        }
        return nullptr;
    }

    auto found = m_gameDbs.find(index);
    if (found != m_gameDbs.end())
    {
        return &found->second;
    }
    return nullptr;
}

const AsyncDBQueue::DbState* AsyncDBQueue::getDbState(EDBType type, int index) const
{
    return const_cast<AsyncDBQueue*>(this)->getDbState(type, index);
}

void AsyncDBQueue::enqueueJob(EDBType type, int index, DbJob job, Callback callback)
{
    bool enqueued = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (getDbState(type, index) != nullptr)
        {
            m_queue.push_back(Request{ type, index, std::move(job), std::move(callback) });
            enqueued = true;
        }
    }

    if (enqueued)
    {
        m_cv.notify_one();
        return;
    }

    // 미등록 DB: 호출부가 HasDatabase로 걸러야 정상. 방어적으로 즉시 실패 콜백 → 코루틴이 영원히 멈추지 않게 한다.
    DBResult errorResult;
    errorResult.errorMsg = std::format("unknown db (type={} index={})", static_cast<int>(type), index);
    if (callback)
    {
        callback(std::move(errorResult));
    }
}

DBResultAwaitable AsyncDBQueue::ExecuteAsync(EDBType dbType, int dbIndex, const std::string& query, std::vector<DBParam> params, IResumeExecutor* pExecutor)
{
    // starter: 단발 쿼리 1개를 실행하는 job을 큐에 넣는다.
    return DBResultAwaitable(
        [this, dbType, dbIndex, query, params = std::move(params)](std::function<void(DBResult)> callback) mutable
        {
            enqueueJob(dbType, dbIndex,
                [capturedQuery = std::move(query), capturedParams = std::move(params)](DBConnection& connection, const DBConnectionConfig&)
                {
                    return connection.Execute(capturedQuery, capturedParams);
                },
                std::move(callback));
        },
        pExecutor
    );
}

DBResultAwaitable AsyncDBQueue::TransactionAsync(EDBType dbType, int dbIndex, TxBody body, IResumeExecutor* pExecutor, TxOptions options)
{
    // starter: BEGIN…(body)…COMMIT + 재시도를 수행하는 job을 큐에 넣는다.
    return DBResultAwaitable(
        [this, dbType, dbIndex, body = std::move(body), options](std::function<void(DBResult)> callback) mutable
        {
            enqueueJob(dbType, dbIndex,
                [capturedBody = std::move(body), options](DBConnection& connection, const DBConnectionConfig& config)
                {
                    return runTransaction(connection, config, capturedBody, options);
                },
                std::move(callback));
        },
        pExecutor
    );
}

bool AsyncDBQueue::HasDatabase(EDBType dbType, int dbIndex) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return getDbState(dbType, dbIndex) != nullptr;
}

void AsyncDBQueue::workerProc()
{
    while (true)
    {
        Request                       request;
        std::unique_ptr<DBConnection> connection;   // 이번에 사용할 커넥션(점유)
        std::shared_ptr<const DBConnectionConfig> config;   // 그 DB의 접속설정(재연결/커넥션 치유용). 락 안에서 refcount 복사만.

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return !m_bRunning.load() || !m_queue.empty(); });

            if (m_queue.empty())
            {
                // 종료 신호 + 잔여 없음이면 끝, 아니면 spurious wakeup
                if (!m_bRunning.load())
                {
                    break;
                }
                continue;
            }

            request = std::move(m_queue.front());
            m_queue.pop_front();

            // 각 DB는 worker 수만큼 커넥션을 보유 → 동시에 점유하는 worker가 최대 worker 수이므로
            // 항상 유휴 커넥션이 1개 이상 남아있다(모자랄 수 없음). 등록 검증은 enqueue 시 끝났다.
            DbState& state = *getDbState(request.type, request.index);
            connection = std::move(state.freeConns.back());
            state.freeConns.pop_back();
            config = state.config;
        }

        // ── 블로킹 실행 (락 밖). job = 단발 쿼리 또는 트랜잭션. ──
        DBResult result = request.job(*connection, *config);
        const unsigned int resultErrorCode = result.errorCode;   // 콜백이 result를 move하기 전에 보관
        if (request.callback)
        {
            request.callback(std::move(result));
        }

        // ── 죽은 커넥션 치유 (락 밖) ──
        // 연결끊김(2006/2013) 후의 커넥션을 그대로 풀에 반납하면 다음 요청들이 연달아 실패한다.
        // 반납 전에 재연결을 시도해 풀 오염을 막는다. (쿼리 재실행이 아니라 커넥션만 살리는 것 → idempotency 무관)
        //   - 연결끊김은 핸들이 아직 열린 채라 errorCode로 판별. (직전 치유가 실패해 핸들이 닫힌 경우는 !IsOpen으로)
        if (isConnectionLost(resultErrorCode) || !connection->IsOpen())
        {
            if (!connection->Open(*config))
            {
                LOG_WRITE(LogLevel::Error, "AsyncDBQueue: dead connection reconnect failed on return (DB down?)");
            }
        }

        // 커넥션 반납(busy → free).
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            getDbState(request.type, request.index)->freeConns.push_back(std::move(connection));
        }
    }
}

} // namespace db
