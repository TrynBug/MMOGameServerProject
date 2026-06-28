#include "pch.h"
#include "DbSaveExecutor.h"
#include "ProtoJsonSerializer.h"   // packet::ProtoJsonSerializer (write 직전 직렬화)

namespace db
{

namespace
{
    // "?, ?, ..., ?" (count개) 를 sql 뒤에 붙인다. (괄호 없음)
    void appendPlaceholders(std::string& sql, size_t count)
    {
        for (size_t index = 0; index < count; ++index)
        {
            if (index > 0)
            {
                sql += ", ?";
            }
            else
            {
                sql += "?";
            }
        }
    }

    // 한 테이블의 upsert 행들을 멀티행 INSERT ... ON DUPLICATE KEY UPDATE 로 실행.
    // 직렬화는 여기서(write 직전) 한다. 성공 true.
    bool execUpserts(db::DBTransaction& transaction, const DbTableInfo& tableInfo,
                     const std::vector<const DbSaveBatch::Entry*>& rows)
    {
        if (rows.empty())
        {
            return true;
        }

        const size_t idColumnCount = rows.front()->idColumns.size();   // 같은 테이블이면 동일
        std::string  jsonBuffer;   // 행 루프 동안 재사용하는 직렬화 버퍼

        std::string sql = "INSERT INTO ";
        sql += tableInfo.name;
        sql += " (";
        sql += tableInfo.insertCols;
        sql += ") VALUES ";

        std::vector<db::DBParam> params;
        params.reserve(rows.size() * (idColumnCount + 1));
        for (size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex)
        {
            if (rowIndex > 0)
            {
                sql += ", ";
            }
            sql += '(';
            appendPlaceholders(sql, idColumnCount + 1);   // 키들 + data
            sql += ')';

            for (const db::DBParam& idColumn : rows[rowIndex]->idColumns)
            {
                params.push_back(idColumn);
            }

            jsonBuffer.clear();
            if (!packet::ProtoJsonSerializer::ToJson(*rows[rowIndex]->proto, jsonBuffer))
            {
                return false;             // 직렬화 실패 → 롤백
            }
            params.push_back(jsonBuffer);    // DBParam(string) 으로 복사
        }
        sql += " AS new ON DUPLICATE KEY UPDATE data = new.data";

        return transaction.Execute(sql, params).success;
    }

    // 한 테이블의 delete 행들을 실행. 단일 PK → WHERE pk IN (...), 복합 PK → WHERE (a,b) IN ((..),..).
    bool execDeletes(db::DBTransaction& transaction, const DbTableInfo& tableInfo,
                     const std::vector<const DbSaveBatch::Entry*>& rows)
    {
        if (rows.empty())
        {
            return true;
        }

        const int pkColumnCount = tableInfo.pkColCount;

        std::string sql = "DELETE FROM ";
        sql += tableInfo.name;
        sql += " WHERE ";
        if (pkColumnCount == 1)
        {
            sql += tableInfo.pkCols;
            sql += " IN (";
            appendPlaceholders(sql, rows.size());
            sql += ")";
        }
        else
        {
            sql += "(";
            sql += tableInfo.pkCols;               // "a, b"
            sql += ") IN (";
            for (size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex)
            {
                if (rowIndex > 0)
                {
                    sql += ", ";
                }
                sql += '(';
                appendPlaceholders(sql, static_cast<size_t>(pkColumnCount));
                sql += ')';
            }
            sql += ")";
        }

        std::vector<db::DBParam> params;
        params.reserve(rows.size() * pkColumnCount);
        for (const DbSaveBatch::Entry* entry : rows)
        {
            for (int columnIndex = 0; columnIndex < pkColumnCount; ++columnIndex)
            {
                params.push_back(entry->idColumns[columnIndex]);   // PK = 키 벡터 앞 N개
            }
        }

        return transaction.Execute(sql, params).success;
    }
}

db::DBResultAwaitable DbSaveExecutor::Save(db::AsyncDBQueue& dbQueue, const std::shared_ptr<DbSaveBatch>& spBatch, int shardIndex, db::IResumeExecutor* resume)
{
    return dbQueue.TransactionAsync(
        db::EDBType::Game, shardIndex,
        [spBatch](db::DBTransaction& transaction) -> bool   // shared_ptr by-value 캡처(수명)
        {
            // Tables() 는 std::map<EDbTable,..> → enum 선언순 순회(테이블 락 순서 일관).
            for (const auto& [tableEnum, rows] : spBatch->Tables())
            {
                const DbTableInfo& tableInfo = GetDbTableInfo(tableEnum);

                // rows(RowMap) 는 PK 오름차순. upsert/delete 로 분할(둘 다 PK 순서 유지).
                std::vector<const DbSaveBatch::Entry*> upserts;
                std::vector<const DbSaveBatch::Entry*> deletes;
                for (const auto& [primaryKey, entry] : rows)
                {
                    if (entry.isDelete)
                    {
                        deletes.push_back(&entry);
                    }
                    else
                    {
                        upserts.push_back(&entry);
                    }
                }

                if (!execUpserts(transaction, tableInfo, upserts))
                {
                    return false;
                }
                if (!execDeletes(transaction, tableInfo, deletes))
                {
                    return false;
                }
            }
            return true;   // COMMIT
        },
        resume);
}

} // namespace db
