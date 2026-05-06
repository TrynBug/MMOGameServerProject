#pragma once

#include "pch.h"
#include "DBTypes.h"
#include "DBConnection.h"

namespace db
{

// 비동기 DB 요청 큐. 내부적으로 worker 스레드들을 관리하며, 각 worker 스레드가 자신의 DBConnection을 사용하여 DB 요청을 처리한다.
// @사용 방법:
//   1. Open(dbPath, numWorkers) 함수로 초기화
//   2. Execute(query, params, callback) 으로 DB 요청을 함. DB 요청은 queue에 들어가며 worker 스레드가 순서대로 처리함.
//   3. DB 처리가 완료되면 callback이 호출된다. 주의: callback은 AsyncDBQueue의 worker 스레드에서 호출됨.
//
// @worker 스레드 수:
//   SQLite는 WAL 모드에서 동시 읽기를 지원하지만, 쓰기는 직렬화된다. 쓰기가 많은 경우에는 1개가 적합하고, 읽기가 많은 경우에는 여러개로 늘려도 됨
class AsyncDBQueue
{
public:
    // callback 타입: DB worker 스레드에서 호출됨
    using Callback = std::function<void(DBResult)>;

public:
    AsyncDBQueue()  = default;
    ~AsyncDBQueue();

    AsyncDBQueue(const AsyncDBQueue&)            = delete;
    AsyncDBQueue& operator=(const AsyncDBQueue&) = delete;

public:
    // DB 파일을 열고 worker 스레드를 시작한다.
    bool Open(const std::string& dbFilePath, int numWorkers = 1);

    // DB 연결을 닫는다. 큐에 남은 요청은 모두 처리한 후 종료된다.
    void Close();

	// 비동기 쿼리 요청. 주의: callback은 DB worker 스레드에서 호출됨
    void Execute(const std::string& query, std::vector<DBParam> params, Callback callback = nullptr);

    // 비동기 쿼리 요청
    void Execute(const std::string& query, std::vector<DBParam> params = {});

    bool IsOpen() const { return m_bRunning.load(); }

private:
    struct Request
    {
        std::string          query;
        std::vector<DBParam> params;
        Callback             callback;
    };

    void workerProc(int workerIndex);

private:
    std::string               m_dbFilePath;
    std::atomic<bool>         m_bRunning { false };

    std::mutex                m_queueMutex;
    std::condition_variable   m_cv;
    std::vector<Request>      m_queue;

    // worker 스레드
    std::vector<std::thread>                    m_workers;     // worker 스레드
    std::vector<std::unique_ptr<DBConnection>>  m_connections; // worker 스레드가 소유하는 DB connection
};

} // namespace db
