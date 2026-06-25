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
    DBResult r = m_conn.Execute(query, params);
    if (!r.success)
    {
        m_failed        = true;
        m_lastErrorCode = r.errorCode;
        m_lastErrorMsg  = r.errorMsg;
    }
    return r;
}

// ─────────────────────────────────────────────────────────────────────────────
// 트랜잭션 실행 (worker 스레드에서 동기로). 일시적 에러면 본문을 재실행한다.
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
    // 재시도 대상 = 타이밍이 원인이라 다시 하면 대개 성공하는 에러.
    //   1213 deadlock / 1205 lock wait timeout / 2006·2013 연결끊김.
    bool isTransient(unsigned int code)
    {
        return code == 1213 || code == 1205 || code == 2006 || code == 2013;
    }
    bool isConnectionLost(unsigned int code)
    {
        return code == 2006 || code == 2013;
    }

    void backoff(int attempt, const AsyncDBQueue::TxOptions& opts)
    {
        // base*attempt + 지터[0,base]. 지터로 동시 재시도가 겹치는 것(thundering herd)을 흩는다.
        thread_local std::mt19937 rng{ std::random_device{}() };
        const int base = opts.backoffBaseMs > 0 ? opts.backoffBaseMs : 1;
        std::uniform_int_distribution<int> jitter(0, base);
        const int ms = base * attempt + jitter(rng);
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }

    DBResult runTransaction(DBConnection& conn, const DBConnectionConfig& cfg,
                            const AsyncDBQueue::TxBody& body, const AsyncDBQueue::TxOptions& opts)
    {
        const int maxAttempts = opts.maxAttempts > 0 ? opts.maxAttempts : 1;

        for (int attempt = 1; attempt <= maxAttempts; ++attempt)
        {
            // 일시적 에러 후 재시도 직전, 다음 시도를 준비하는 공통 처리.
            //   연결끊김이면 재연결 시도, 그 외 일시적이면 백오프. 더 못 하면 false 반환.
            auto prepareRetry = [&](unsigned int code) -> bool
            {
                if (!isTransient(code) || attempt >= maxAttempts)
                    return false;
                if (isConnectionLost(code))
                    conn.Open(cfg);   // close + 재연결 (실패해도 다음 시도의 쿼리가 에러로 드러난다)
                backoff(attempt, opts);
                return true;
            };

            DBResult begin = conn.Begin();
            if (!begin.success)
            {
                if (prepareRetry(begin.errorCode)) continue;
                DBResult out;
                out.errorCode = begin.errorCode;
                out.errorMsg  = std::format("BEGIN failed: {}", begin.errorMsg);
                return out;
            }

            DBTransaction tx(conn);
            bool commitIntent = false;
            try
            {
                commitIntent = body(tx);
            }
            catch (const std::exception& e)
            {
                conn.Rollback();
                DBResult out;
                out.errorMsg = std::format("transaction body threw: {}", e.what());
                return out;   // 예외는 재시도하지 않는다(원인 불명).
            }
            catch (...)
            {
                conn.Rollback();
                DBResult out;
                out.errorMsg = "transaction body threw unknown exception";
                return out;
            }

            // 본문 안에서 쿼리가 실패했다면 절대 커밋하지 않는다.
            if (tx.Failed())
            {
                conn.Rollback();
                if (prepareRetry(tx.LastErrorCode())) continue;
                DBResult out;
                out.errorCode = tx.LastErrorCode();   // 결정적 에러(1062 등) → 즉시 실패
                out.errorMsg  = tx.LastErrorMsg();
                return out;
            }

            // 본문이 롤백을 선택(비즈니스 중단) → 재시도 없이 실패 반환(errorCode=0).
            if (!commitIntent)
            {
                conn.Rollback();
                DBResult out;
                out.errorMsg = "aborted by transaction body";
                return out;
            }

            DBResult commit = conn.Commit();
            if (!commit.success)
            {
                conn.Rollback();   // best-effort
                if (prepareRetry(commit.errorCode)) continue;
                DBResult out;
                out.errorCode = commit.errorCode;
                out.errorMsg  = std::format("COMMIT failed: {}", commit.errorMsg);
                return out;
            }

            DBResult out;
            out.success = true;   // 커밋 완료
            return out;
        }

        DBResult out;
        out.errorMsg = "transaction max attempts exhausted";
        return out;
    }
} // anonymous namespace

AsyncDBQueue::~AsyncDBQueue()
{
    Close();
}

bool AsyncDBQueue::Open(const std::vector<OpenEntry>& entries, int numWorkers)
{
    if (m_bRunning)
        return true;

    if (entries.empty())
    {
        m_lastError = "no databases given";
        return false;
    }
    if (numWorkers < 1) numWorkers = 1;

    auto clearAll = [this] { m_accountDb.reset(); m_gameDbs.clear(); };

    // 각 DB의 DbState를 만들고 커넥션 numWorkers개를 미리 연다.
    // 워커 수 = numWorkers이고 워커는 요청당 커넥션 1개만 점유하므로, 한 DB로 모든 워커가
    // 동시에 몰려도 커넥션이 모자랄 수 없다(별도 cap/상한 불필요).
    for (const auto& e : entries)
    {
        if (e.type == EDBType::Game && e.index < 0)
        {
            m_lastError = std::format("invalid game db index {}", e.index);
            clearAll();
            return false;
        }

        const bool duplicate = (e.type == EDBType::Account) ? m_accountDb.has_value()
                                                            : m_gameDbs.contains(e.index);
        if (duplicate)
        {
            m_lastError = std::format("duplicate db (type={} index={})", static_cast<int>(e.type), e.index);
            clearAll();
            return false;
        }

        DbState st;
        st.config = e.config;
        st.freeConns.reserve(numWorkers);
        for (int i = 0; i < numWorkers; ++i)
        {
            auto conn = std::make_unique<DBConnection>();
            if (!conn->Open(e.config))
            {
                m_lastError = std::format("db (type={} index={}) connect failed: {}",
                    static_cast<int>(e.type), e.index, conn->GetLastError());
                clearAll();
                return false;
            }
            st.freeConns.push_back(std::move(conn));
        }

        if (e.type == EDBType::Account)
            m_accountDb = std::move(st);
        else
            m_gameDbs.emplace(e.index, std::move(st));
    }

    m_bRunning = true;

    m_workers.reserve(numWorkers);
    for (int i = 0; i < numWorkers; ++i)
        m_workers.emplace_back(&AsyncDBQueue::workerProc, this);

    return true;
}

bool AsyncDBQueue::Open(const DBConnectionConfig& config, int numWorkers)
{
    std::vector<OpenEntry> entries{ { EDBType::Account, 0, config } };
    return Open(entries, numWorkers);
}

void AsyncDBQueue::Close()
{
    if (!m_bRunning.exchange(false))
        return;

    m_cv.notify_all();

    for (auto& thread : m_workers)
    {
        if (thread.joinable())
            thread.join();
    }

    m_workers.clear();
    m_accountDb.reset();
    m_gameDbs.clear();
    m_queue.clear();
}

AsyncDBQueue::DbState* AsyncDBQueue::getDbState(EDBType type, int index)
{
    if (type == EDBType::Account)
        return m_accountDb ? &*m_accountDb : nullptr;

    auto it = m_gameDbs.find(index);
    return it != m_gameDbs.end() ? &it->second : nullptr;
}

const AsyncDBQueue::DbState* AsyncDBQueue::getDbState(EDBType type, int index) const
{
    return const_cast<AsyncDBQueue*>(this)->getDbState(type, index);
}

void AsyncDBQueue::enqueueJob(EDBType type, int index, DbJob job, Callback cb)
{
    bool enqueued = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (getDbState(type, index) != nullptr)
        {
            m_queue.push_back(Request{ type, index, std::move(job), std::move(cb) });
            enqueued = true;
        }
    }

    if (enqueued)
    {
        m_cv.notify_one();
        return;
    }

    // 미등록 DB: 호출부가 HasDatabase로 걸러야 정상. 방어적으로 즉시 실패 콜백 → 코루틴이 영원히 멈추지 않게 한다.
    DBResult err;
    err.errorMsg = std::format("unknown db (type={} index={})", static_cast<int>(type), index);
    if (cb)
        cb(std::move(err));
}

DBResultAwaitable AsyncDBQueue::ExecuteAsync(EDBType dbType, int dbIndex, const std::string& query, std::vector<DBParam> params, IResumeExecutor* pExecutor)
{
    // starter: 단발 쿼리 1개를 실행하는 job을 큐에 넣는다.
    return DBResultAwaitable(
        [this, dbType, dbIndex, query, params = std::move(params)](std::function<void(DBResult)> cb) mutable
        {
            enqueueJob(dbType, dbIndex,
                [q = std::move(query), p = std::move(params)](DBConnection& c, const DBConnectionConfig&)
                {
                    return c.Execute(q, p);
                },
                std::move(cb));
        },
        pExecutor
    );
}

DBResultAwaitable AsyncDBQueue::TransactionAsync(EDBType dbType, int dbIndex, TxBody body, IResumeExecutor* pExecutor, TxOptions opts)
{
    // starter: BEGIN…(body)…COMMIT + 재시도를 수행하는 job을 큐에 넣는다.
    return DBResultAwaitable(
        [this, dbType, dbIndex, body = std::move(body), opts](std::function<void(DBResult)> cb) mutable
        {
            enqueueJob(dbType, dbIndex,
                [body = std::move(body), opts](DBConnection& c, const DBConnectionConfig& cfg)
                {
                    return runTransaction(c, cfg, body, opts);
                },
                std::move(cb));
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
        Request                       req;
        std::unique_ptr<DBConnection> conn;   // 이번에 사용할 커넥션(점유)
        DBConnectionConfig            cfg;    // 그 DB의 접속설정(트랜잭션 재연결용). 락 안에서 복사.

        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return !m_bRunning.load() || !m_queue.empty(); });

            if (m_queue.empty())
            {
                // 종료 신호 + 잔여 없음이면 끝, 아니면 spurious wakeup
                if (!m_bRunning.load())
                    break;
                continue;
            }

            req = std::move(m_queue.front());
            m_queue.pop_front();

            // 각 DB는 worker 수만큼 커넥션을 보유 → 동시에 점유하는 worker가 최대 worker 수이므로
            // 항상 유휴 커넥션이 1개 이상 남아있다(모자랄 수 없음). 등록 검증은 enqueue 시 끝났다.
            DbState& s = *getDbState(req.type, req.index);
            conn = std::move(s.freeConns.back());
            s.freeConns.pop_back();
            cfg  = s.config;
        }

        // ── 블로킹 실행 (락 밖). job = 단발 쿼리 또는 트랜잭션. ──
        DBResult result = req.job(*conn, cfg);
        if (req.callback)
            req.callback(std::move(result));

        // 커넥션 반납(busy → free).
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            getDbState(req.type, req.index)->freeConns.push_back(std::move(conn));
        }
    }
}

} // namespace db
