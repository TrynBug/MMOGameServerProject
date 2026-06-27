#pragma once

#include "pch.h"
#include "DBTypes.h"

// MySQL C API 타입 전방선언
struct MYSQL;
struct MYSQL_STMT;

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

    // 트랜잭션 제어 (text 프로토콜). 한 커넥션에서 BEGIN…COMMIT 으로 여러 쿼리를 묶을 때 사용.
    // 보통 AsyncDBQueue::TransactionAsync 가 내부에서 호출하므로 직접 쓸 일은 드물다.
    DBResult Begin();      // START TRANSACTION
    DBResult Commit();
    DBResult Rollback();

    // 마지막 오류 메시지
    std::string GetLastError() const;

private:
    DBResult fetchResult(MYSQL_STMT* pStatement);
    DBResult runControl(const char* sql);   // Begin/Commit/Rollback 공통(text 프로토콜)

    MYSQL*      m_pDb = nullptr;
    std::string m_lastError;   // 연결 실패 등으로 핸들을 닫은 뒤에도 사유를 보관(GetLastError용)
};

} // namespace db
