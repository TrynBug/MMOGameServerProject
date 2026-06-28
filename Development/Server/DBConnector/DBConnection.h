#pragma once

#include "pch.h"
#include "DBTypes.h"

// MySQL C API 타입 전방선언
struct MYSQL;
struct MYSQL_STMT;
struct MYSQL_RES;

namespace db
{

// MySQL 연결 1개를 래핑하는 클래스
// DB 요청을 동기로 처리함 (비동기 처리는 AsyncDBQueue 사용)
// thread-safe 하지 않음. 반드시 1개 스레드에서만 사용할 것.
class DBConnection
{
public:
    DBConnection()  = default;
    ~DBConnection();

    DBConnection(const DBConnection&)            = delete;
    DBConnection& operator=(const DBConnection&) = delete;

public:
    // MySQL 서버에 연결한다.
    bool Open(const DBConnectionConfig& config);

    void Close();

    bool IsOpen() const { return m_pDb != nullptr; }

    // 쿼리 실행 (prepared statement + 파라미터 바인딩)
    DBResult Execute(const std::string& query, const std::vector<DBParam>& params = {});

    // 멀티문장(여러 SELECT 를 ';' 로 이은 쿼리)을 **한 왕복**으로 실행하고 결과셋을 문장 순서대로 돌려준다.
    // text 프로토콜(mysql_real_query) 사용 → prepared statement/파라미터 바인딩 없음. 호출자가 SQL 을 안전하게 조립해야 한다
    // (이 프로젝트에서는 키가 정수라 직접 삽입 → 인젝션 없음). 배치 읽기(DbLoadExecutor)용.
    std::vector<DBResult> ExecuteMulti(const std::string& multiQuery);

    // 트랜잭션 제어 (text 프로토콜). 한 커넥션에서 BEGIN…COMMIT 으로 여러 쿼리를 묶을 때 사용.
    // 보통 AsyncDBQueue::TransactionAsync 가 내부에서 호출하므로 직접 쓸 일은 드물다.
    DBResult Begin();      // START TRANSACTION
    DBResult Commit();
    DBResult Rollback();

    // 마지막 오류 메시지
    std::string GetLastError() const;

private:
    DBResult fetchResult(MYSQL_STMT* pStatement);
    DBResult textResultToDBResult(MYSQL_RES* pResultSet);   // text 프로토콜(mysql_store_result) 결과 → DBResult
    DBResult runControl(const char* sql);   // Begin/Commit/Rollback 공통(text 프로토콜)

    // 같은 SQL 의 prepared statement 를 재사용하려고 캐시에서 찾고, 없으면 prepare 해서 캐시에 넣는다.
    // 실패 시 nullptr 반환 + outError 에 사유 기록.
    MYSQL_STMT* acquireStatement(const std::string& query, DBResult& outError);

    MYSQL*      m_pDb = nullptr;
    std::string m_lastError;   // 연결 실패 등으로 핸들을 닫은 뒤에도 사유를 보관(GetLastError용)

    // SQL → 그 SQL 로 prepare 된 statement. 커넥션 1개를 1스레드가 단독 사용하므로 락 불필요.
    // 커넥션이 닫히거나 재연결되면(Close) 전부 폐기된다(끊긴 핸들의 statement 는 무효).
    std::unordered_map<std::string, MYSQL_STMT*> m_statementCache;
};

} // namespace db
