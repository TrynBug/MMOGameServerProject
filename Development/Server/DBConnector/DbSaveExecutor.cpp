#include "pch.h"
#include "DbSaveExecutor.h"
#include "ProtoJsonSerializer.h"   // packet::ProtoJsonSerializer (write 직전 직렬화)

namespace db
{

namespace
{
    // "?, ?, ..., ?" (n개) 를 sql 뒤에 붙인다. (괄호 없음)
    void appendQ(std::string& sql, size_t n)
    {
        for (size_t i = 0; i < n; ++i)
            sql += (i ? ", ?" : "?");
    }

    // 한 테이블의 upsert 행들을 멀티행 INSERT ... ON DUPLICATE KEY UPDATE 로 실행.
    // 직렬화는 여기서(write 직전) 재사용 버퍼 jsonBuf 로. 성공 true.
    bool execUpserts(db::DBTransaction& tx, const DbTableInfo& info,
                     const std::vector<const DbSaveBatch::Entry*>& rows, std::string& jsonBuf)
    {
        if (rows.empty())
            return true;

        const size_t keyCount = rows.front()->keys.size();   // 같은 테이블이면 동일

        std::string sql = "INSERT INTO ";
        sql += info.name;
        sql += " (";
        sql += info.insertCols;
        sql += ") VALUES ";

        std::vector<db::DBParam> params;
        params.reserve(rows.size() * (keyCount + 1));
        for (size_t i = 0; i < rows.size(); ++i)
        {
            if (i) sql += ", ";
            sql += '(';
            appendQ(sql, keyCount + 1);   // 키들 + data
            sql += ')';

            for (const db::DBParam& k : rows[i]->keys)
                params.push_back(k);

            jsonBuf.clear();
            if (!packet::ProtoJsonSerializer::ToJson(*rows[i]->proto, jsonBuf))
                return false;             // 직렬화 실패 → 롤백
            params.push_back(jsonBuf);    // DBParam(string) 으로 복사
        }
        sql += " AS new ON DUPLICATE KEY UPDATE data = new.data";

        return tx.Execute(sql, params).success;
    }

    // 한 테이블의 delete 행들을 실행. 단일 PK → WHERE pk IN (...), 복합 PK → WHERE (a,b) IN ((..),..).
    bool execDeletes(db::DBTransaction& tx, const DbTableInfo& info,
                     const std::vector<const DbSaveBatch::Entry*>& rows)
    {
        if (rows.empty())
            return true;

        const int pkCols = info.pkColCount;

        std::string sql = "DELETE FROM ";
        sql += info.name;
        sql += " WHERE ";
        if (pkCols == 1)
        {
            sql += info.pkCols;
            sql += " IN (";
            appendQ(sql, rows.size());
            sql += ")";
        }
        else
        {
            sql += "(";
            sql += info.pkCols;               // "a, b"
            sql += ") IN (";
            for (size_t i = 0; i < rows.size(); ++i)
            {
                if (i) sql += ", ";
                sql += '(';
                appendQ(sql, static_cast<size_t>(pkCols));
                sql += ')';
            }
            sql += ")";
        }

        std::vector<db::DBParam> params;
        params.reserve(rows.size() * pkCols);
        for (const DbSaveBatch::Entry* e : rows)
            for (int c = 0; c < pkCols; ++c)
                params.push_back(e->keys[c]);   // PK = 키 벡터 앞 N개

        return tx.Execute(sql, params).success;
    }
}

db::DBResultAwaitable DbSaveExecutor::Save(db::AsyncDBQueue& dbq, const std::shared_ptr<DbSaveBatch>& spBatch, int shardIndex, db::IResumeExecutor* resume)
{
    return dbq.TransactionAsync(
        db::EDBType::Game, shardIndex,
        [spBatch](db::DBTransaction& tx) -> bool   // shared_ptr by-value 캡처(수명)
        {
            std::string jsonBuf;   // 직렬화 재사용 버퍼

            // Tables() 는 std::map<EDbTable,..> → enum 선언순 순회(테이블 락 순서 일관).
            for (const auto& [tableEnum, rows] : spBatch->Tables())
            {
                const DbTableInfo& info = GetDbTableInfo(tableEnum);

                // rows(RowMap) 는 PK 오름차순. upsert/delete 로 분할(둘 다 PK 순서 유지).
                std::vector<const DbSaveBatch::Entry*> upserts, deletes;
                for (const auto& [pk, entry] : rows)
                    (entry.isDelete ? deletes : upserts).push_back(&entry);

                if (!execUpserts(tx, info, upserts, jsonBuf))
                    return false;
                if (!execDeletes(tx, info, deletes))
                    return false;
            }
            return true;   // COMMIT
        },
        resume);
}

} // namespace db
