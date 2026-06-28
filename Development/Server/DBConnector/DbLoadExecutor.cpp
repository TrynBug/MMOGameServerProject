#include "pch.h"
#include "DbLoadExecutor.h"

namespace db
{

db::AwaitableCoTask<DbLoadResult> DbLoadExecutor::Load(db::AsyncDBQueue& dbQueue, const std::shared_ptr<DbLoadBatch>& spBatch, db::EDBType dbType, int dbIndex, db::IResumeExecutor* resume)
{
    auto spResult = std::make_shared<DbLoadResult>();

    // 읽기도 쓰기(DbSaveExecutor)와 동일하게 **TransactionAsync** 한 경로로 처리한다.
    //   - body 안에서 모든 SELECT 를 **멀티문장 1회**(tx.ExecuteMultiStatement)로 실행 → 테이블 수와 무관하게 BEGIN+멀티문장+COMMIT(3왕복).
    //   - 한 트랜잭션이라 모든 테이블을 한 스냅샷에서 일관 로드.
    //   - JSON 역직렬화는 body(= DB 워커 스레드)에서 끝낸다(게임로직 스레드 파싱 0).
    db::DBResult txResult = co_await dbQueue.TransactionAsync(
        dbType, dbIndex,
        [spBatch, spResult](db::DBTransaction& transaction) -> bool
        {
            spResult->Clear();   // [필수] 재시도 replay 대비: 채우기 전 초기화

            const std::vector<DbLoadBatch::Request>& requests = spBatch->Requests();
            if (requests.empty())
            {
                return true;   // 로드할 것 없음 → 빈 결과로 커밋
            }

            // 여러 SELECT 를 한 멀티문장으로 합친다. 키는 정수(account/character id)라 직접 삽입(인젝션 없음).
            std::string multiQuery;
            for (const DbLoadBatch::Request& request : requests)
            {
                multiQuery += "SELECT data FROM ";
                multiQuery += request.tableName;
                multiQuery += " WHERE ";
                multiQuery += request.keyCol;
                multiQuery += " = ";
                multiQuery += std::to_string(request.keyValue);
                multiQuery += ";";
            }

            // 한 왕복으로 실행 → 결과셋들(요청과 같은 순서). 실패가 있으면 transaction 이 내부에 사유 기록(→ 롤백/재시도 분류).
            std::vector<db::DBResult> resultSets = transaction.ExecuteMultiStatement(multiQuery);
            if (resultSets.size() != requests.size())
            {
                return false;   // 개수 불일치(중간 에러 등) → 롤백
            }

            // 각 결과셋을 해당 요청의 parse 로 워커 스레드에서 역직렬화.
            for (size_t requestIndex = 0; requestIndex < requests.size(); ++requestIndex)
            {
                const DbLoadBatch::Request& request = requests[requestIndex];
                const db::DBResult&         set     = resultSets[requestIndex];
                if (!set.success)
                {
                    return false;
                }

                for (int rowIndex = 0; rowIndex < set.RowCount(); ++rowIndex)
                {
                    bool parsed = false;
                    auto message = request.parse(set.GetString(rowIndex, "data"), parsed);   // ← 워커 스레드 FromJson
                    if (!parsed)
                    {
                        return false;   // 파싱 실패 → 롤백(재시도 안 함; DB 에러 아님)
                    }
                    spResult->Add(request.table, std::move(message));
                }
            }
            return true;   // COMMIT
        },
        resume);

    spResult->success = txResult.success;
    co_return std::move(*spResult);
}

} // namespace db
