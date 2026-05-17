#include "pch.h"
#include "AsyncDBQueue.h"

namespace db
{

AsyncDBQueue::~AsyncDBQueue()
{
    Close();
}

bool AsyncDBQueue::Open(const std::string& dbFilePath, int numWorkers)
{
    if (m_bRunning)
        return true;

    m_dbFilePath = dbFilePath;

	// DB Connection 생성
    m_connections.resize(numWorkers);
    for (int i = 0; i < numWorkers; ++i)
    {
        m_connections[i] = std::make_unique<DBConnection>();
        if (!m_connections[i]->Open(dbFilePath))
            return false;
    }

    m_bRunning = true;

    // worker 스레드 시작
    m_workers.reserve(numWorkers);
    for (int i = 0; i < numWorkers; ++i)
        m_workers.emplace_back(&AsyncDBQueue::workerProc, this, i);

    return true;
}

void AsyncDBQueue::Close()
{
    if (!m_bRunning.exchange(false))
        return;

    // 모든 worker 스레드 깨움
    m_cv.notify_all();

    for (auto& thread : m_workers)
    {
        if (thread.joinable())
            thread.join();
    }

    m_workers.clear();
    m_connections.clear();
    m_queue.clear();
}

void AsyncDBQueue::Execute(const std::string& query, std::vector<DBParam> params, Callback callback)
{
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_queue.push_back({ query, std::move(params), std::move(callback) });
    }

    m_cv.notify_one();
}

void AsyncDBQueue::Execute(const std::string& query, std::vector<DBParam> params)
{
    Execute(query, std::move(params), nullptr);
}

// worker 스레드 루틴
void AsyncDBQueue::workerProc(int workerIndex)
{
    DBConnection& conn = *m_connections[workerIndex];

    while (true)
    {
        Request req;

        // 요청이 생길 때까지 대기
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            m_cv.wait(lock, [this]
            {
                return !m_queue.empty() || !m_bRunning.load();
            });

            if (m_queue.empty())
            {
				// 큐가 비어있고 종료 플래그가 켜졌으면 종료
                if (!m_bRunning)
                    break;
                continue;
            }

            req = std::move(m_queue.front());
            m_queue.erase(m_queue.begin());
        }

        // 쿼리 실행
        DBResult result = conn.Execute(req.query, req.params);

        // 콜백 호출
        if (req.callback)
            req.callback(std::move(result));
    }
}

DBResultAwaitable AsyncDBQueue::ExecuteAsync(const std::string& query, std::vector<DBParam> params, IResumeExecutor* pExecutor)
{
    return DBResultAwaitable(
        query,
        std::move(params),
        [this](std::string q, std::vector<DBParam> p, std::function<void(DBResult)> cb)
        {
            Execute(std::move(q), std::move(p), std::move(cb));
        },
        pExecutor
    );
}

} // namespace db
