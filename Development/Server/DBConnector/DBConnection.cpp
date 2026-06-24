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
    DBValue fieldToDBValue(const MYSQL_FIELD& field, const std::string& s)
    {
        switch (field.type)
        {
        case MYSQL_TYPE_TINY:
        case MYSQL_TYPE_SHORT:
        case MYSQL_TYPE_LONG:
        case MYSQL_TYPE_LONGLONG:
        case MYSQL_TYPE_INT24:
        case MYSQL_TYPE_YEAR:
            try { return static_cast<int64_t>(std::stoll(s)); } catch (...) { return s; }

        case MYSQL_TYPE_FLOAT:
        case MYSQL_TYPE_DOUBLE:
            try { return std::stod(s); } catch (...) { return s; }

        case MYSQL_TYPE_TINY_BLOB:
        case MYSQL_TYPE_MEDIUM_BLOB:
        case MYSQL_TYPE_LONG_BLOB:
        case MYSQL_TYPE_BLOB:
            // charset 63 = binary → 진짜 BLOB. 그 외(charset 있음)는 TEXT 이므로 문자열로.
            if (field.charsetnr == 63)
                return std::vector<uint8_t>(s.begin(), s.end());
            return s;

        // VARCHAR / CHAR / JSON / DATETIME / TIMESTAMP / DECIMAL 등은 문자열로.
        default:
            return s;
        }
    }
}

DBConnection::~DBConnection()
{
    Close();
}

bool DBConnection::Open(const DBConnectionConfig& cfg)
{
    if (m_pDb)
        Close();

    m_pDb = mysql_init(nullptr);
    if (!m_pDb)
        return false;

    // utf8mb4 (JSON/한글 안전)
    mysql_options(m_pDb, MYSQL_SET_CHARSET_NAME, "utf8mb4");

    // caching_sha2_password(MySQL 8 기본 인증)를 비-TLS 연결에서도 쓰도록 서버 공개키 요청 허용.
    bool getPubKey = true;
    mysql_options(m_pDb, MYSQL_OPT_GET_SERVER_PUBLIC_KEY, &getPubKey);

    if (!mysql_real_connect(
            m_pDb,
            cfg.host.c_str(),
            cfg.user.c_str(),
            cfg.password.c_str(),
            cfg.database.empty() ? nullptr : cfg.database.c_str(),
            cfg.port,
            nullptr, 0))
    {
        // 실패 사유를 핸들 닫기 전에 보관 (errno + 메시지)
        m_lastError = std::format("mysql_real_connect failed ({}): {} [host={} port={} user={} db={}]",
            mysql_errno(m_pDb), mysql_error(m_pDb), cfg.host, cfg.port, cfg.user, cfg.database);
        mysql_close(m_pDb);
        m_pDb = nullptr;
        return false;
    }

    m_lastError.clear();
    return true;
}

void DBConnection::Close()
{
    if (m_pDb)
    {
        mysql_close(m_pDb);
        m_pDb = nullptr;
    }
}

DBResult DBConnection::Execute(const std::string& query, const std::vector<DBParam>& params)
{
    DBResult result;

    if (!m_pDb)
    {
        result.errorMsg = "DB not open";
        return result;
    }

    MYSQL_STMT* pStmt = mysql_stmt_init(m_pDb);
    if (!pStmt)
    {
        result.errorMsg = mysql_error(m_pDb);
        return result;
    }

    if (mysql_stmt_prepare(pStmt, query.c_str(), static_cast<unsigned long>(query.size())) != 0)
    {
        result.errorMsg = mysql_stmt_error(pStmt);
        mysql_stmt_close(pStmt);
        return result;
    }

    // ── 파라미터 바인딩 ──
    // MYSQL_BIND 와 보조 저장소(값/길이)는 execute 까지 유효해야 하므로 함수 스코프에 둔다.
    const size_t paramCount = params.size();
    std::vector<MYSQL_BIND>    paramBinds(paramCount);
    std::vector<long long>     i64store(paramCount, 0);
    std::vector<double>        dblstore(paramCount, 0.0);
    std::vector<unsigned long> paramLen(paramCount, 0);
    if (paramCount > 0)
        std::memset(paramBinds.data(), 0, paramCount * sizeof(MYSQL_BIND));

    for (size_t i = 0; i < paramCount; ++i)
    {
        MYSQL_BIND& b = paramBinds[i];
        std::visit([&](const auto& val)
        {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, std::monostate>)
            {
                b.buffer_type = MYSQL_TYPE_NULL;
            }
            else if constexpr (std::is_same_v<T, int64_t>)
            {
                i64store[i]   = val;
                b.buffer_type = MYSQL_TYPE_LONGLONG;
                b.buffer      = &i64store[i];
            }
            else if constexpr (std::is_same_v<T, double>)
            {
                dblstore[i]   = val;
                b.buffer_type = MYSQL_TYPE_DOUBLE;
                b.buffer      = &dblstore[i];
            }
            else if constexpr (std::is_same_v<T, std::string>)
            {
                paramLen[i]     = static_cast<unsigned long>(val.size());
                b.buffer_type   = MYSQL_TYPE_STRING;
                b.buffer        = const_cast<char*>(val.data());   // params 가 execute 까지 살아있어 안전
                b.buffer_length = paramLen[i];
                b.length        = &paramLen[i];
            }
            else if constexpr (std::is_same_v<T, std::vector<uint8_t>>)
            {
                paramLen[i]     = static_cast<unsigned long>(val.size());
                b.buffer_type   = MYSQL_TYPE_BLOB;
                b.buffer        = const_cast<uint8_t*>(val.data());
                b.buffer_length = paramLen[i];
                b.length        = &paramLen[i];
            }
        }, params[i]);
    }

    if (paramCount > 0 && mysql_stmt_bind_param(pStmt, paramBinds.data()) != 0)
    {
        result.errorMsg = mysql_stmt_error(pStmt);
        mysql_stmt_close(pStmt);
        return result;
    }

    if (mysql_stmt_execute(pStmt) != 0)
    {
        result.errorMsg = mysql_stmt_error(pStmt);
        mysql_stmt_close(pStmt);
        return result;
    }

    result = fetchResult(pStmt);

    mysql_stmt_close(pStmt);
    return result;
}

DBResult DBConnection::fetchResult(MYSQL_STMT* pStmt)
{
    DBResult result;

    MYSQL_RES* pMeta = mysql_stmt_result_metadata(pStmt);
    if (!pMeta)
    {
        // 결과셋 없음 (INSERT/UPDATE/DELETE 등) → 성공.
        result.success = true;
        return result;
    }

    const unsigned int colCount = mysql_num_fields(pMeta);
    MYSQL_FIELD* fields = mysql_fetch_fields(pMeta);

    std::vector<std::string> colNames(colCount);
    for (unsigned int i = 0; i < colCount; ++i)
        colNames[i] = fields[i].name ? fields[i].name : "";

    // 결과 바인딩: 모든 컬럼을 0-length 버퍼로 바인딩 → fetch 시 실제 길이가 length 에 채워지면
    // mysql_stmt_fetch_column 으로 가변길이 데이터를 안전하게 읽는다.
    std::vector<MYSQL_BIND>    binds(colCount);
    std::vector<unsigned long> lengths(colCount, 0);
    auto isNull = std::make_unique<bool[]>(colCount);
    auto errs   = std::make_unique<bool[]>(colCount);
    std::memset(binds.data(), 0, colCount * sizeof(MYSQL_BIND));

    for (unsigned int i = 0; i < colCount; ++i)
    {
        binds[i].buffer_type   = MYSQL_TYPE_STRING;   // 문자열로 받아 타입별 변환
        binds[i].buffer        = nullptr;
        binds[i].buffer_length = 0;
        binds[i].length        = &lengths[i];
        binds[i].is_null       = &isNull[i];
        binds[i].error         = &errs[i];
    }

    if (mysql_stmt_bind_result(pStmt, binds.data()) != 0)
    {
        result.errorMsg = mysql_stmt_error(pStmt);
        mysql_free_result(pMeta);
        return result;
    }

    // 전체 결과를 클라이언트에 버퍼링 (fetch_column 안정성).
    if (mysql_stmt_store_result(pStmt) != 0)
    {
        result.errorMsg = mysql_stmt_error(pStmt);
        mysql_free_result(pMeta);
        return result;
    }

    while (true)
    {
        const int fetchRc = mysql_stmt_fetch(pStmt);
        if (fetchRc == MYSQL_NO_DATA)
            break;
        if (fetchRc != 0 && fetchRc != MYSQL_DATA_TRUNCATED)
        {
            // 0-length 바인딩이라 MYSQL_DATA_TRUNCATED 는 정상. 그 외는 진짜 에러.
            result.errorMsg = mysql_stmt_error(pStmt);
            mysql_free_result(pMeta);
            return result;
        }

        DBRow row;
        for (unsigned int i = 0; i < colCount; ++i)
        {
            if (isNull[i])
            {
                row[colNames[i]] = std::monostate{};
                continue;
            }

            // 실제 데이터를 길이만큼 가변 버퍼로 읽는다.
            const unsigned long len = lengths[i];
            std::string buf;
            buf.resize(len);

            MYSQL_BIND col;
            std::memset(&col, 0, sizeof(col));
            unsigned long realLen = 0;
            bool          colNull = false;
            col.buffer_type   = MYSQL_TYPE_STRING;
            col.buffer        = len ? buf.data() : nullptr;
            col.buffer_length = len;
            col.length        = &realLen;
            col.is_null       = &colNull;

            if (mysql_stmt_fetch_column(pStmt, &col, i, 0) != 0)
            {
                row[colNames[i]] = std::monostate{};
                continue;
            }

            row[colNames[i]] = fieldToDBValue(fields[i], buf);
        }

        result.rows.push_back(std::move(row));
    }

    result.success = true;
    mysql_free_result(pMeta);
    return result;
}

int64_t DBConnection::LastInsertRowId() const
{
    return m_pDb ? static_cast<int64_t>(mysql_insert_id(m_pDb)) : 0;
}

std::string DBConnection::GetLastError() const
{
    if (m_pDb)
        return mysql_error(m_pDb);
    return m_lastError.empty() ? "DB not open" : m_lastError;
}

} // namespace db
