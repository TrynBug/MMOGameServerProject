#include "pch.h"
#include "AsyncDBQueue.h"

#include <format>

namespace db
{

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

DBResultAwaitable AsyncDBQueue::ExecuteAsync(EDBType dbType, int dbIndex, const std::string& query, std::vector<DBParam> params, IResumeExecutor* pExecutor)
{
    return DBResultAwaitable(
        query,
        std::move(params),
        // 전역 큐에 요청을 적재하고 worker를 깨운다. cb(완료 콜백)는 worker 스레드에서 호출되어 코루틴을 resume한다.
        [this, dbType, dbIndex](std::string q, std::vector<DBParam> p, std::function<void(DBResult)> cb)
        {
            bool enqueued = false;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (getDbState(dbType, dbIndex) != nullptr)
                {
                    m_queue.push_back(Request{ dbType, dbIndex, std::move(q), std::move(p), std::move(cb) });
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
            err.success  = false;
            err.errorMsg = std::format("unknown db (type={} index={})", static_cast<int>(dbType), dbIndex);
            if (cb)
                cb(std::move(err));
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
        }

        // ── 블로킹 실행 (락 밖) ──
        DBResult result = conn->Execute(req.query, req.params);
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
