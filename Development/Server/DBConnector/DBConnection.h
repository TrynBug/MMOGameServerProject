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

    // 마지막 AUTO_INCREMENT 값 (snowflake ID 사용 시 보통 불필요)
    int64_t LastInsertRowId() const;

    // 마지막 오류 메시지
    std::string GetLastError() const;

private:
    DBResult fetchResult(MYSQL_STMT* pStmt);

    MYSQL*      m_pDb = nullptr;
    std::string m_lastError;   // 연결 실패 등으로 핸들을 닫은 뒤에도 사유를 보관(GetLastError용)
};

} // namespace db
