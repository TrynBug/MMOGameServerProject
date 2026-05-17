#pragma once

#include "pch.h"

namespace db
{

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

// 쿼리 결과 전체
struct DBResult
{
public:
    bool success = false;
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
