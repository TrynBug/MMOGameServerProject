#include "pch.h"
#include "DBConnection.h"

namespace db
{

DBConnection::~DBConnection()
{
    Close();
}

bool DBConnection::Open(const std::string& filePath)
{
    if (m_pDb)
        Close();

    // DB 파일 열기. 파일이 없으면 새로 생성한다.
    int rc = sqlite3_open(filePath.c_str(), &m_pDb);
    if (rc != SQLITE_OK)
    {
        m_pDb = nullptr;
        return false;
    }

    // WAL 모드 활성화 (동시읽기 성능 향상, 쓰기 충돌 감소)
    sqlite3_exec(m_pDb, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

    // Foreign key 제약 활성화
    sqlite3_exec(m_pDb, "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr);

    return true;
}

void DBConnection::Close()
{
    if (m_pDb)
    {
        sqlite3_close(m_pDb);
        m_pDb = nullptr;
    }
}

// 쿼리 실행
DBResult DBConnection::Execute(const std::string& query, const std::vector<DBParam>& params)
{
    DBResult result;

    if (!m_pDb)
    {
        result.errorMsg = "DB not open";
        return result;
    }

    sqlite3_stmt* pStmt = nullptr;
    int rc = sqlite3_prepare_v2(m_pDb, query.c_str(), -1, &pStmt, nullptr);
    if (rc != SQLITE_OK)
    {
        result.errorMsg = sqlite3_errmsg(m_pDb);
        return result;
    }

    // 파라미터 바인딩 (1-based index)
    for (int i = 0; i < static_cast<int>(params.size()); ++i)
        bindParam(pStmt, i + 1, params[i]);

    result = fetchResult(pStmt);
    sqlite3_finalize(pStmt);
    return result;
}

int64_t DBConnection::LastInsertRowId() const
{
    return m_pDb ? sqlite3_last_insert_rowid(m_pDb) : 0;
}

std::string DBConnection::GetLastError() const
{
    return m_pDb ? sqlite3_errmsg(m_pDb) : "DB not open";
}

void DBConnection::bindParam(sqlite3_stmt* pStmt, int index, const DBParam& param)
{
    std::visit([&](const auto& val)
    {
        using T = std::decay_t<decltype(val)>;

        if constexpr (std::is_same_v<T, std::monostate>)
        {
            sqlite3_bind_null(pStmt, index);
        }
        else if constexpr (std::is_same_v<T, int64_t>)
        {
            sqlite3_bind_int64(pStmt, index, val);
        }
        else if constexpr (std::is_same_v<T, double>)
        {
            sqlite3_bind_double(pStmt, index, val);
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
            sqlite3_bind_text(pStmt, index, val.c_str(), static_cast<int>(val.size()), SQLITE_TRANSIENT);
        }
        else if constexpr (std::is_same_v<T, std::vector<uint8_t>>)
        {
            sqlite3_bind_blob(pStmt, index, val.data(), static_cast<int>(val.size()), SQLITE_TRANSIENT);
        }
    }, param);
}

DBResult DBConnection::fetchResult(sqlite3_stmt* pStmt)
{
    DBResult result;

    // 컬럼 이름 수집
    int colCount = sqlite3_column_count(pStmt);
    std::vector<std::string> colNames;
    colNames.reserve(colCount);
    for (int i = 0; i < colCount; ++i)
        colNames.emplace_back(sqlite3_column_name(pStmt, i));

    // 행 순회
    while (true)
    {
        int rc = sqlite3_step(pStmt);

        if (rc == SQLITE_ROW)
        {
            DBRow row;
            for (int i = 0; i < colCount; ++i)
            {
                int colType = sqlite3_column_type(pStmt, i);
                DBValue val;

                switch (colType)
                {
                case SQLITE_INTEGER:
                    val = sqlite3_column_int64(pStmt, i);
                    break;
                case SQLITE_FLOAT:
                    val = sqlite3_column_double(pStmt, i);
                    break;
                case SQLITE_TEXT:
                {
                    const char* pText = reinterpret_cast<const char*>(sqlite3_column_text(pStmt, i));
                    val = pText ? std::string(pText) : std::string{};
                    break;
                }
                case SQLITE_BLOB:
                {
                    const uint8_t* pBlob = static_cast<const uint8_t*>(sqlite3_column_blob(pStmt, i));
                    int blobSize = sqlite3_column_bytes(pStmt, i);
                    val = std::vector<uint8_t>(pBlob, pBlob + blobSize);
                    break;
                }
                case SQLITE_NULL:
                default:
                    val = std::monostate{};
                    break;
                }

                row[colNames[i]] = std::move(val);
            }

            result.rows.push_back(std::move(row));
        }
        else if (rc == SQLITE_DONE)
        {
            result.success = true;
            break;
        }
        else
        {
            result.errorMsg = sqlite3_errmsg(m_pDb);
            break;
        }
    }

    return result;
}

} // namespace db
