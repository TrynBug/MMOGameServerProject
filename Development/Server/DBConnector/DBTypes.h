#pragma once

#include "pch.h"

namespace db
{

// DB 종류. ExecuteAsync 등에서 어느 DB로 보낼지 식별한다.
//   - Account : 계정/전역 DB (1개). dbIndex는 무시된다.
//   - Game    : 게임 데이터 샤드. dbIndex(=game_db_index)로 어느 샤드인지 지정한다.
enum class EDBType
{
    Account,
    Game,
};

// DB 컬럼 값 타입
// SQLite의 컬럼 타입 5가지(INTEGER, REAL, TEXT, BLOB, NULL)에 대응한다.
using DBValue = std::variant<
    std::monostate,   // NULL
    int64_t,          // INTEGER
    double,           // REAL
    std::string,      // TEXT
    std::vector<uint8_t>  // BLOB
>;

// DB 테이블 row 1개 (Key=컬럼명, Value=값)
using DBRow = std::unordered_map<std::string, DBValue>;

// 바인딩 파라미터 타입 (쿼리의 ? 자리에 들어갈 값)
using DBParam = DBValue;

// MySQL 연결 설정 (DBConnection::Open / AsyncDBQueue::Open 에 전달)
struct DBConnectionConfig
{
    std::string  host     = "127.0.0.1";
    unsigned int port     = 3306;
    std::string  user;
    std::string  password;
    std::string  database;   // 스키마명(샤딩 시 GameDB_XX). 비우면 DB 미지정 연결.
};

// 쿼리 결과 전체
struct DBResult
{
public:
    bool success = false;
    unsigned int errorCode = 0;   // 실패 시 mysql_errno (재시도 분류 + 1062 중복키 등 판별). 0이면 DB에러 아님.
    std::string errorMsg;
    std::vector<DBRow> rows;

public:
    bool IsEmpty() const { return rows.empty(); }
    int RowCount() const { return static_cast<int>(rows.size()); }

    // 1개 row에서 1개 컬럼값 얻기 (rowIndex 또는 col이 유효하지 않으면 defaultValue을 반환함)
    int64_t GetInt64(int rowIndex, const std::string& column, int64_t defaultValue = 0) const;
    double GetDouble(int rowIndex, const std::string& column, double defaultValue = 0.0) const;
    std::string GetString(int rowIndex, const std::string& column, std::string defaultValue = "") const;
    bool IsNull(int rowIndex, const std::string& column) const;

private:
    const DBValue* getValue(int rowIndex, const std::string& column) const;
};

} // namespace db
