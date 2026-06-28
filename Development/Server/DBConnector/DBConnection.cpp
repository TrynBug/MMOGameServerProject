#include "pch.h"
#include "DBConnection.h"

#include <mysql.h>
#include <cstring>
#include <format>

#pragma comment(lib, "libmysql.lib")

namespace db
{

namespace
{
    // MySQL 컬럼(필드)의 문자열 표현을 DBValue 로 변환한다.
    // prepared statement 결과를 모두 문자열로 받아온 뒤, 필드 타입에 맞춰 int64/double/string/blob 으로 변환.
    DBValue fieldToDBValue(const MYSQL_FIELD& field, const std::string& fieldText)
    {
        switch (field.type)
        {
        case MYSQL_TYPE_TINY:
        case MYSQL_TYPE_SHORT:
        case MYSQL_TYPE_LONG:
        case MYSQL_TYPE_LONGLONG:
        case MYSQL_TYPE_INT24:
        case MYSQL_TYPE_YEAR:
            try
            {
                return static_cast<int64_t>(std::stoll(fieldText));
            }
            catch (...)
            {
                return fieldText;
            }

        case MYSQL_TYPE_FLOAT:
        case MYSQL_TYPE_DOUBLE:
            try
            {
                return std::stod(fieldText);
            }
            catch (...)
            {
                return fieldText;
            }

        case MYSQL_TYPE_TINY_BLOB:
        case MYSQL_TYPE_MEDIUM_BLOB:
        case MYSQL_TYPE_LONG_BLOB:
        case MYSQL_TYPE_BLOB:
            // charset 63 = binary → 진짜 BLOB. 그 외(charset 있음)는 TEXT 이므로 문자열로.
            if (field.charsetnr == 63)
            {
                return std::vector<uint8_t>(fieldText.begin(), fieldText.end());
            }
            return fieldText;

        // VARCHAR / CHAR / JSON / DATETIME / TIMESTAMP / DECIMAL 등은 문자열로.
        default:
            return fieldText;
        }
    }
}

DBConnection::~DBConnection()
{
    Close();
}

bool DBConnection::Open(const DBConnectionConfig& config)
{
    if (m_pDb)
    {
        Close();
    }

    m_pDb = mysql_init(nullptr);
    if (!m_pDb)
    {
        return false;
    }

    // utf8mb4 (JSON/한글 안전)
    mysql_options(m_pDb, MYSQL_SET_CHARSET_NAME, "utf8mb4");

    // caching_sha2_password(MySQL 8 기본 인증)를 비-TLS 연결에서도 쓰도록 서버 공개키 요청 허용.
    bool getPublicKey = true;
    mysql_options(m_pDb, MYSQL_OPT_GET_SERVER_PUBLIC_KEY, &getPublicKey);

    // database 가 비어있으면 DB 미지정 연결(nullptr), 아니면 스키마명을 넘긴다.
    const char* databaseName = nullptr;
    if (!config.database.empty())
    {
        databaseName = config.database.c_str();
    }

    if (!mysql_real_connect(
            m_pDb,
            config.host.c_str(),
            config.user.c_str(),
            config.password.c_str(),
            databaseName,
            config.port,
            nullptr, 0))
    {
        // 실패 사유를 핸들 닫기 전에 보관 (errno + 메시지)
        m_lastError = std::format("mysql_real_connect failed ({}): {} [host={} port={} user={} db={}]",
            mysql_errno(m_pDb), mysql_error(m_pDb), config.host, config.port, config.user, config.database);
        mysql_close(m_pDb);
        m_pDb = nullptr;
        return false;
    }

    m_lastError.clear();
    return true;
}

void DBConnection::Close()
{
    // 캐시된 prepared statement 를 먼저 닫는다(핸들을 닫기 전에). 재연결 시에도 Open 이 Close 를 호출하므로
    // 끊긴 커넥션의 statement 가 남지 않는다.
    for (auto& [query, pStatement] : m_statementCache)
    {
        mysql_stmt_close(pStatement);
    }
    m_statementCache.clear();

    if (m_pDb)
    {
        mysql_close(m_pDb);
        m_pDb = nullptr;
    }
}

// 같은 SQL 이면 캐시된 statement 를 재사용(prepare 왕복 절약). 없으면 새로 prepare 해서 캐시에 넣는다.
MYSQL_STMT* DBConnection::acquireStatement(const std::string& query, DBResult& outError)
{
    auto found = m_statementCache.find(query);
    if (found != m_statementCache.end())
    {
        return found->second;
    }

    MYSQL_STMT* pStatement = mysql_stmt_init(m_pDb);
    if (!pStatement)
    {
        outError.errorCode = mysql_errno(m_pDb);
        outError.errorMsg  = mysql_error(m_pDb);
        return nullptr;
    }

    if (mysql_stmt_prepare(pStatement, query.c_str(), static_cast<unsigned long>(query.size())) != 0)
    {
        outError.errorCode = mysql_stmt_errno(pStatement);
        outError.errorMsg  = mysql_stmt_error(pStatement);
        mysql_stmt_close(pStatement);   // prepare 실패한 statement 는 캐시하지 않는다.
        return nullptr;
    }

    m_statementCache.emplace(query, pStatement);
    return pStatement;
}

DBResult DBConnection::Execute(const std::string& query, const std::vector<DBParam>& params)
{
    DBResult result;

    if (!m_pDb)
    {
        result.errorMsg = "DB not open";
        return result;
    }

    // 같은 SQL 의 prepared statement 를 커넥션별로 캐시·재사용한다(반복 쿼리의 prepare 왕복 절약).
    MYSQL_STMT* pStatement = acquireStatement(query, result);
    if (!pStatement)
    {
        return result;   // result 에 에러가 채워져 있음
    }

    // ── 파라미터 바인딩 ──
    // MYSQL_BIND 와 보조 저장소(값/길이)는 execute 까지 유효해야 하므로 함수 스코프에 둔다.
    const size_t paramCount = params.size();
    std::vector<MYSQL_BIND>    parameterBindings(paramCount);
    std::vector<long long>     int64Storage(paramCount, 0);
    std::vector<double>        doubleStorage(paramCount, 0.0);
    std::vector<unsigned long> parameterLengths(paramCount, 0);
    if (paramCount > 0)
    {
        std::memset(parameterBindings.data(), 0, paramCount * sizeof(MYSQL_BIND));
    }

    for (size_t paramIndex = 0; paramIndex < paramCount; ++paramIndex)
    {
        MYSQL_BIND& binding = parameterBindings[paramIndex];
        std::visit([&](const auto& value)
        {
            using ValueType = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<ValueType, std::monostate>)
            {
                binding.buffer_type = MYSQL_TYPE_NULL;
            }
            else if constexpr (std::is_same_v<ValueType, int64_t>)
            {
                int64Storage[paramIndex] = value;
                binding.buffer_type      = MYSQL_TYPE_LONGLONG;
                binding.buffer           = &int64Storage[paramIndex];
            }
            else if constexpr (std::is_same_v<ValueType, double>)
            {
                doubleStorage[paramIndex] = value;
                binding.buffer_type       = MYSQL_TYPE_DOUBLE;
                binding.buffer            = &doubleStorage[paramIndex];
            }
            else if constexpr (std::is_same_v<ValueType, std::string>)
            {
                parameterLengths[paramIndex] = static_cast<unsigned long>(value.size());
                binding.buffer_type          = MYSQL_TYPE_STRING;
                binding.buffer               = const_cast<char*>(value.data());   // params 가 execute 까지 살아있어 안전
                binding.buffer_length        = parameterLengths[paramIndex];
                binding.length               = &parameterLengths[paramIndex];
            }
            else if constexpr (std::is_same_v<ValueType, std::vector<uint8_t>>)
            {
                parameterLengths[paramIndex] = static_cast<unsigned long>(value.size());
                binding.buffer_type          = MYSQL_TYPE_BLOB;
                binding.buffer               = const_cast<uint8_t*>(value.data());
                binding.buffer_length        = parameterLengths[paramIndex];
                binding.length               = &parameterLengths[paramIndex];
            }
        }, params[paramIndex]);
    }

    if (paramCount > 0 && mysql_stmt_bind_param(pStatement, parameterBindings.data()) != 0)
    {
        result.errorCode = mysql_stmt_errno(pStatement);
        result.errorMsg  = mysql_stmt_error(pStatement);
        return result;   // 캐시된 statement 는 닫지 않는다(statement 자체는 유효, 재사용).
    }

    if (mysql_stmt_execute(pStatement) != 0)
    {
        result.errorCode = mysql_stmt_errno(pStatement);   // 데드락(1213)/락대기(1205) 등이 여기서 드러남
        result.errorMsg  = mysql_stmt_error(pStatement);
        return result;   // 연결레벨 에러(2006/2013)면 호출측이 재연결 → Close 가 statement 캐시를 비운다.
    }

    result = fetchResult(pStatement);

    // 결과 자원만 풀고 statement 는 캐시에 열린 채 보관(다음 execute 에 재사용).
    mysql_stmt_free_result(pStatement);
    return result;
}

DBResult DBConnection::fetchResult(MYSQL_STMT* pStatement)
{
    DBResult result;

    MYSQL_RES* pMetadata = mysql_stmt_result_metadata(pStatement);
    if (!pMetadata)
    {
        // 결과셋 없음 (INSERT/UPDATE/DELETE 등) → 성공.
        result.success = true;
        return result;
    }

    const unsigned int columnCount = mysql_num_fields(pMetadata);
    MYSQL_FIELD* fields = mysql_fetch_fields(pMetadata);

    std::vector<std::string> columnNames(columnCount);
    for (unsigned int columnIndex = 0; columnIndex < columnCount; ++columnIndex)
    {
        if (fields[columnIndex].name)
        {
            columnNames[columnIndex] = fields[columnIndex].name;
        }
        else
        {
            columnNames[columnIndex] = "";
        }
    }

    // 결과 바인딩: 모든 컬럼을 0-length 버퍼로 바인딩 → fetch 시 실제 길이가 length 에 채워지면
    // mysql_stmt_fetch_column 으로 가변길이 데이터를 안전하게 읽는다.
    std::vector<MYSQL_BIND>    resultBindings(columnCount);
    std::vector<unsigned long> columnLengths(columnCount, 0);
    auto columnIsNullFlags = std::make_unique<bool[]>(columnCount);
    auto columnErrorFlags  = std::make_unique<bool[]>(columnCount);
    std::memset(resultBindings.data(), 0, columnCount * sizeof(MYSQL_BIND));

    for (unsigned int columnIndex = 0; columnIndex < columnCount; ++columnIndex)
    {
        resultBindings[columnIndex].buffer_type   = MYSQL_TYPE_STRING;   // 문자열로 받아 타입별 변환
        resultBindings[columnIndex].buffer        = nullptr;
        resultBindings[columnIndex].buffer_length = 0;
        resultBindings[columnIndex].length        = &columnLengths[columnIndex];
        resultBindings[columnIndex].is_null       = &columnIsNullFlags[columnIndex];
        resultBindings[columnIndex].error         = &columnErrorFlags[columnIndex];
    }

    if (mysql_stmt_bind_result(pStatement, resultBindings.data()) != 0)
    {
        result.errorCode = mysql_stmt_errno(pStatement);
        result.errorMsg  = mysql_stmt_error(pStatement);
        mysql_free_result(pMetadata);
        return result;
    }

    // 전체 결과를 클라이언트에 버퍼링 (fetch_column 안정성).
    if (mysql_stmt_store_result(pStatement) != 0)
    {
        result.errorCode = mysql_stmt_errno(pStatement);
        result.errorMsg  = mysql_stmt_error(pStatement);
        mysql_free_result(pMetadata);
        return result;
    }

    while (true)
    {
        const int fetchResultCode = mysql_stmt_fetch(pStatement);
        if (fetchResultCode == MYSQL_NO_DATA)
        {
            break;
        }
        if (fetchResultCode != 0 && fetchResultCode != MYSQL_DATA_TRUNCATED)
        {
            // 0-length 바인딩이라 MYSQL_DATA_TRUNCATED 는 정상. 그 외는 진짜 에러.
            result.errorCode = mysql_stmt_errno(pStatement);
            result.errorMsg  = mysql_stmt_error(pStatement);
            mysql_free_result(pMetadata);
            return result;
        }

        DBRow row;
        for (unsigned int columnIndex = 0; columnIndex < columnCount; ++columnIndex)
        {
            if (columnIsNullFlags[columnIndex])
            {
                row[columnNames[columnIndex]] = std::monostate{};
                continue;
            }

            // 실제 데이터를 길이만큼 가변 버퍼로 읽는다.
            const unsigned long columnLength = columnLengths[columnIndex];
            std::string columnBuffer;
            columnBuffer.resize(columnLength);

            MYSQL_BIND columnBinding;
            std::memset(&columnBinding, 0, sizeof(columnBinding));
            unsigned long actualLength = 0;
            bool          columnIsNull = false;
            columnBinding.buffer_type   = MYSQL_TYPE_STRING;
            if (columnLength > 0)
            {
                columnBinding.buffer = columnBuffer.data();
            }
            else
            {
                columnBinding.buffer = nullptr;
            }
            columnBinding.buffer_length = columnLength;
            columnBinding.length        = &actualLength;
            columnBinding.is_null       = &columnIsNull;

            if (mysql_stmt_fetch_column(pStatement, &columnBinding, columnIndex, 0) != 0)
            {
                row[columnNames[columnIndex]] = std::monostate{};
                continue;
            }

            row[columnNames[columnIndex]] = fieldToDBValue(fields[columnIndex], columnBuffer);
        }

        result.rows.push_back(std::move(row));
    }

    result.success = true;
    mysql_free_result(pMetadata);
    return result;
}

DBResult DBConnection::runControl(const char* sql)
{
    DBResult result;

    if (!m_pDb)
    {
        result.errorMsg = "DB not open";
        return result;
    }

    // 트랜잭션 제어문은 text 프로토콜로 보낸다(prepared statement 불필요/엣지케이스 회피).
    if (mysql_real_query(m_pDb, sql, static_cast<unsigned long>(std::strlen(sql))) != 0)
    {
        result.errorCode = mysql_errno(m_pDb);   // 연결끊김(2006/2013)도 여기로
        result.errorMsg  = mysql_error(m_pDb);
        return result;
    }

    result.success = true;
    return result;
}

DBResult DBConnection::Begin()    { return runControl("START TRANSACTION"); }
DBResult DBConnection::Commit()   { return runControl("COMMIT"); }
DBResult DBConnection::Rollback() { return runControl("ROLLBACK"); }

std::string DBConnection::GetLastError() const
{
    if (m_pDb)
    {
        return mysql_error(m_pDb);
    }
    if (m_lastError.empty())
    {
        return "DB not open";
    }
    return m_lastError;
}

} // namespace db
