#include "pch.h"
#include "DBTypes.h"

namespace db
{

// 1개 row에서 1개 컬럼값을 int64로 얻기 (rowIndex 또는 column이 유효하지 않으면 defaultValue을 반환함)
int64_t DBResult::GetInt64(int rowIndex, const std::string& column, int64_t defaultValue) const
{
    const DBValue* pDBValue = getValue(rowIndex, column);
    if (!pDBValue)
        return defaultValue;

    // INTEGER -> int64 변환
    if (const int64_t* pVal = std::get_if<int64_t>(pDBValue))
        return *pVal;

    // REAL -> int64 변환
    if (const double* pVal = std::get_if<double>(pDBValue))
        return static_cast<int64_t>(*pVal);

    return defaultValue;
}

// 1개 row에서 1개 컬럼값을 double로 얻기 (rowIndex 또는 column이 유효하지 않으면 defaultValue을 반환함)
double DBResult::GetDouble(int rowIndex, const std::string& column, double defaultValue) const
{
    const DBValue* pDBValue = getValue(rowIndex, column);
    if (!pDBValue)
        return defaultValue;

    // REAL -> double 변환
    if (const double* pVal = std::get_if<double>(pDBValue))
        return *pVal;

    // INTEGER -> double 변환
    if (const int64_t* pVal = std::get_if<int64_t>(pDBValue))
        return static_cast<double>(*pVal);

    return defaultValue;
}

// 1개 row에서 1개 컬럼값을 string으로 얻기 (rowIndex 또는 column이 유효하지 않으면 defaultValue을 반환함)
std::string DBResult::GetString(int rowIndex, const std::string& column, std::string defaultValue) const
{
    const DBValue* pDBValue = getValue(rowIndex, column);
    if (!pDBValue)
        return defaultValue;

	// TEXT -> string 변환
    if (auto* pVal = std::get_if<std::string>(pDBValue))
        return *pVal;

    return defaultValue;
}

// NULL 여부 확인 (기본값: true)
bool DBResult::IsNull(int rowIndex, const std::string& column) const
{
    const DBValue* pDBValue = getValue(rowIndex, column);
    if (!pDBValue)
        return true;

	// variant에 아무값도 입력된적 없다면 std::monostate를 가지므로 variant가 std::monostate인지 확인하여 NULL 여부를 판단한다.
    if (auto* pVal = std::get_if<std::monostate>(pDBValue))
        return true;

    return false;
}

// 1개 row에서 1개 컬럼값 얻기
const DBValue* DBResult::getValue(int rowIndex, const std::string& column) const
{
    if (rowIndex < 0 || rowIndex >= static_cast<int>(rows.size()))
        return nullptr;

    auto iter = rows[rowIndex].find(column);
    if (iter == rows[rowIndex].end())
        return nullptr;

    return &iter->second;
}

} // namespace db
