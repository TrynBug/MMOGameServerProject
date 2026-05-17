#pragma once

#include "pch.h"
#include "DBTypes.h"

namespace db
{

// SQLite 연결 1개를 래핑하는 클래스
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
    // DB 파일 열기. 파일이 없으면 새로 생성한다.
    bool Open(const std::string& filePath);

    void Close();

    bool IsOpen() const { return m_pDb != nullptr; }

    // 쿼리 실행
    DBResult Execute(const std::string& query, const std::vector<DBParam>& params = {});

    // 마지막으로 삽입된 rowid
    int64_t LastInsertRowId() const;

    // 마지막 오류 메시지
    std::string GetLastError() const;

private:
    void bindParam(sqlite3_stmt* pStmt, int index, const DBParam& param);
    DBResult fetchResult(sqlite3_stmt* pStmt);

    sqlite3* m_pDb = nullptr;
};

} // namespace db
