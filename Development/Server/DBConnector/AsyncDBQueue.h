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

// ─────────────────────────────────────────────────────────────────────────────
// DBTransaction
//
// TransactionAsync의 본문(TxBody)에 넘겨지는 핸들. 한 커넥션에 고정되어 있으며,
// Execute로 여러 쿼리를 같은 트랜잭션 안에서 실행한다. (BEGIN/COMMIT/ROLLBACK은 프레임워크가 처리)
//
// Execute가 DB 에러를 만나면 내부에 errorCode를 기록한다 → runTransaction이 이를 보고
// 일시적 에러(데드락 등)면 본문을 재실행, 결정적 에러(중복키 등)면 즉시 실패시킨다.
//
// [주의] 본문은 worker 스레드에서 동기로, 재시도 시 여러 번 실행된다.
//   - 순수 DB 작업만 할 것(게임오브젝트/Stage 상태 접근·패킷 전송 등 부수효과 금지).
//   - 출력은 캡처한 지역변수에 기록하되, 결과 success==true일 때만 유효하다.
// ─────────────────────────────────────────────────────────────────────────────
class DBTransaction
{
public:
    explicit DBTransaction(DBConnection& conn) : m_conn(conn) {}

    DBTransaction(const DBTransaction&)            = delete;
    DBTransaction& operator=(const DBTransaction&) = delete;

    // 트랜잭션 안에서 쿼리 실행. 실패하면 내부에 사유를 기록한다.
    DBResult Execute(const std::string& query, const std::vector<DBParam>& params = {});

    // 트랜잭션 안에서 멀티문장(여러 SELECT 를 ';' 로 이은 쿼리)을 **한 왕복**으로 실행하고 결과셋들을 문장 순서대로 돌려준다.
    // 배치 읽기(DbLoadExecutor)용. 결과셋 중 하나라도 실패하면 내부에 사유를 기록한다(Execute 와 동일).
    std::vector<DBResult> ExecuteMulti(const std::string& multiQuery);

    bool         Failed()        const { return m_failed; }
    unsigned int LastErrorCode() const { return m_lastErrorCode; }
    const std::string& LastErrorMsg() const { return m_lastErrorMsg; }

private:
    DBConnection& m_conn;
    bool          m_failed        = false;
    unsigned int  m_lastErrorCode = 0;
    std::string   m_lastErrorMsg;
};

// 여러 DB(AccountDB, GameDB 샤드, 기타)를 하나의 공유 워커 풀로 처리하는 비동기 DB 큐.
// 서버는 이 큐를 "1개만" 가지며, (EDBType, dbIndex)로 어느 DB에 보낼지 지정한다.
//
// @사용:
//   1) Open(databases, numWorkers)  // dbKey → 연결설정.
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

    // 트랜잭션 본문. true 반환=커밋 의도 / false 반환=롤백(비즈니스 중단, 재시도 안 함).
    using TxBody = std::function<bool(DBTransaction&)>;

    // 트랜잭션 재시도 옵션.
    struct TxOptions
    {
        int maxAttempts   = 4;   // 일시적 에러 시 본문 재실행 최대 횟수
        int backoffBaseMs = 5;   // 백오프 기본 ms (실제 대기 = base*attempt + 지터)
    };

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

    // 닫는다. 전역 큐에 남은 요청을 모두 처리한 뒤 종료한다.
    void Close();

    // 코루틴용 비동기 쿼리 요청. co_await으로 DBResult를 기다린다.
    //   dbType=Account → dbIndex 무시 / dbType=Game → dbIndex(=game_db_index)로 샤드 선택.
    DBResultAwaitable ExecuteAsync(EDBType dbType, int dbIndex, const std::string& query, std::vector<DBParam> params = {}, IResumeExecutor* pExecutor = nullptr);

    // 코루틴용 비동기 트랜잭션. 한 커넥션에서 BEGIN…(body)…COMMIT 으로 묶고, 일시적 에러는 본문을 재실행한다.
    //   결과 해석: success==true → 커밋됨 / success==false && errorCode==0 → 비즈니스 롤백(body가 false 반환)
    //              / success==false && errorCode!=0 → DB 에러(재시도 소진 포함).
    //   body는 worker 스레드에서 동기로(재시도 시 여러 번) 실행된다. DBTransaction 주석의 작성 규칙 참고.
    DBResultAwaitable TransactionAsync(EDBType dbType, int dbIndex, TxBody body, IResumeExecutor* pExecutor = nullptr, TxOptions opts = {});

    // 해당 DB가 등록되어 있는지. (호출부가 ExecuteAsync 전에 fail-fast 검증)
    bool HasDatabase(EDBType dbType, int dbIndex) const;

    bool IsOpen() const { return m_bRunning.load(); }

    // Open 실패 시 마지막 사유.
    const std::string& GetLastError() const { return m_lastError; }

private:
    // worker가 점유한 커넥션에서 실행하는 작업 단위. (cfg = 그 DB의 접속설정 — 트랜잭션 재연결용)
    //   단발 쿼리든 트랜잭션이든 worker는 job 하나만 호출하면 되므로 트랜잭션을 따로 몰라도 된다.
    using DbJob = std::function<DBResult(DBConnection&, const DBConnectionConfig&)>;

    struct Request
    {
        EDBType  type;
        int      index;   // Game은 game_db_index. Account는 무시(getDbState가 0으로 정규화).
        DbJob    job;
        Callback callback;
    };

    // DB 1개의 커넥션 풀. 모든 접근은 m_mutex 하에서.
    // 커넥션 수는 항상 numWorkers개(Open에서 전부 열고, 점유/반납만 일어난다).
    struct DbState
    {
        // 접속설정은 shared_ptr 로 보유 → worker 가 락 밖에서 refcount 복사만으로 안전하게 들고 쓴다(문자열 복사 없음, 수명 보장).
        std::shared_ptr<const DBConnectionConfig>  config;
        std::vector<std::unique_ptr<DBConnection>> freeConns;   // 유휴 커넥션
    };

    void workerProc();

    // job을 해당 DB의 전역 큐에 적재하고 worker를 깨운다. 미등록 DB면 즉시 실패 콜백.
    void enqueueJob(EDBType type, int index, DbJob job, Callback callback);

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
