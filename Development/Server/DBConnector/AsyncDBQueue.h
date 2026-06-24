#pragma once

#include "pch.h"
#include "DBTypes.h"
#include "DBConnection.h"
#include "DBTask.h"

#include <deque>
#include <vector>
#include <optional>
#include <unordered_map>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace db
{

// 여러 DB(AccountDB, GameDB 샤드, 기타)를 하나의 공유 워커 풀로 처리하는 비동기 DB 큐.
// 서버는 이 큐를 "1개만" 가지며, dbKey(std::pair<EDBType, int>)로 어느 DB에 보낼지 지정한다.
//
// @사용:
//   1) Open(databases, numWorkers)  // dbKey → 연결설정. (단일 DB는 Open(config, ...) 편의 오버로드)
//   2) co_await ExecuteAsync(dbType, dbIndex, query, params, executor)  // 코루틴
//
// @설계 (단순 버전):
//   - 요청 큐는 모든 DB가 공유하는 "전역 FIFO 1개"다. Request가 자기 DbKey를 들고 다닌다.
//   - 워커 스레드 풀은 DB에 묶지 않고 공유한다. 워커는 전역 큐 front를 꺼내 그 요청의 DB로 실행한다.
//   - 각 DB는 Open 시 numWorkers개의 커넥션을 미리 전부 연다. 워커 수 = numWorkers이고 워커는
//     요청당 커넥션 1개만 점유하므로, 한 DB로 모든 워커가 동시에 몰려도 커넥션이 모자랄 수 없다.
//     → cap(동시 실행 상한)이나 runnable 선택, lazy 커넥션 생성이 전부 불필요해진다.
//   - 커넥션 총수 = (DB 수) × numWorkers. 모두 Open에서 미리 열어둔다.
//
// [트레이드오프] 전역 큐라 DB별 격리가 없다. 한 DB(샤드)가 느려지면 그 쿼리를 실행 중인 워커가
// 묶이고, 전역 큐라 건강한 DB의 요청도 그 뒤에서 대기할 수 있다(head-of-line). 현재는 모든 샤드가
// 한 MySQL 인스턴스(다중 스키마)에 있어 격리 이득이 작으므로 단순함을 택했다. 샤드를 별도 호스트로
// 분리해 격리가 필요해지면 DB별 큐 + DB별 동시 실행 상한(cap) 구조를 재도입한다.
class AsyncDBQueue
{
public:
    using Callback = std::function<void(DBResult)>;

    // Open에 넘기는 DB 1개의 등록 정보. (type=Account면 index 무시)
    struct OpenEntry
    {
        EDBType            type;
        int                index;   // Game 샤드 인덱스(=game_db_index). Account면 무시.
        DBConnectionConfig config;
    };

public:
    AsyncDBQueue()  = default;
    ~AsyncDBQueue();

    AsyncDBQueue(const AsyncDBQueue&)            = delete;
    AsyncDBQueue& operator=(const AsyncDBQueue&) = delete;

public:
    // 여러 DB를 등록하고 공유 워커 풀을 시작한다.
    //   numWorkers : 공유 워커 스레드 수. 각 DB는 이 수만큼 커넥션을 미리 연다(커넥션 부족 불가).
    // Open 시 각 DB의 커넥션을 전부 열어 연결성을 검증한다(실패 시 false, 사유는 GetLastError).
    bool Open(const std::vector<OpenEntry>& entries, int numWorkers);

    // 단일 DB 편의 오버로드. AccountDB 1개로 등록한다. (단독 테스트/단일 DB 서버용)
    bool Open(const DBConnectionConfig& config, int numWorkers = 1);

    // 닫는다. 전역 큐에 남은 요청을 모두 처리한 뒤 종료한다.
    void Close();

    // 코루틴용 비동기 쿼리 요청. co_await으로 DBResult를 기다린다.
    //   dbType=Account → dbIndex 무시 / dbType=Game → dbIndex(=game_db_index)로 샤드 선택.
    DBResultAwaitable ExecuteAsync(EDBType dbType, int dbIndex, const std::string& query, std::vector<DBParam> params = {}, IResumeExecutor* pExecutor = nullptr);

    // 해당 DB가 등록되어 있는지. (호출부가 ExecuteAsync 전에 fail-fast 검증)
    bool HasDatabase(EDBType dbType, int dbIndex) const;

    bool IsOpen() const { return m_bRunning.load(); }

    // Open 실패 시 마지막 사유.
    const std::string& GetLastError() const { return m_lastError; }

private:
    struct Request
    {
        EDBType              type;
        int                  index;   // Game은 game_db_index. Account는 무시(getDbState가 0으로 정규화).
        std::string          query;
        std::vector<DBParam> params;
        Callback             callback;
    };

    // DB 1개의 커넥션 풀. 모든 접근은 m_mutex 하에서.
    // 커넥션 수는 항상 numWorkers개(Open에서 전부 열고, 점유/반납만 일어난다).
    struct DbState
    {
        DBConnectionConfig                         config;
        std::vector<std::unique_ptr<DBConnection>> freeConns;   // 유휴 커넥션
    };

    void workerProc();

    // 등록된 DB의 DbState를 얻는 유일한 접근자. 없으면 nullptr. (m_mutex 하에서)
    //   Account → 싱글톤 m_accountDb (index 무시) / Game → m_gameDbs[index]
    DbState*       getDbState(EDBType type, int index);
    const DbState* getDbState(EDBType type, int index) const;

private:
    // Account는 싱글톤(0 또는 1개), Game은 game_db_index로 키잉된 sparse 집합. 성격이 달라 따로 둔다.
    std::optional<DbState>           m_accountDb;    // engaged면 AccountDB 등록됨
    std::unordered_map<int, DbState> m_gameDbs;      // game_db_index → 커넥션 풀 (키 없으면 미등록)
    std::deque<Request>              m_queue;        // 모든 DB가 공유하는 전역 FIFO

    mutable std::mutex               m_mutex;
    std::condition_variable          m_cv;
    std::atomic<bool>                m_bRunning { false };
    std::vector<std::thread>         m_workers;
    std::string                      m_lastError;
};

} // namespace db
