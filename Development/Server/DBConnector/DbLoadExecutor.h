#pragma once

#include "pch.h"
#include "DbLoadBatch.h"
#include "DbLoadResult.h"
#include "AsyncDBQueue.h"   // db::AsyncDBQueue, db::IResumeExecutor
#include "DBTask.h"         // db::AwaitableCoTask

// ─────────────────────────────────────────────────────────────────────────────
// DbLoadExecutor — DbLoadBatch 의 모든 SELECT 를 한 샤드에서 한 DB job 으로 실행하고,
// **DB 워커 스레드에서 JSON 역직렬화까지 끝내** proto 로 담아 돌려준다.
//
// 핵심: 역직렬화(FromJson)가 워커 스레드에서 일어나므로, 게임로직 스레드(IOCP/Stage)는
//       파싱 비용 0 으로 이미 만들어진 proto 만 받는다(쓰기측이 ToJson 을 워커에서 하는 것과 대칭).
// ─────────────────────────────────────────────────────────────────────────────

namespace db
{

class DbLoadExecutor
{
public:
    // co_await 으로 DbLoadResult 를 기다린다. result.success==true 면 전부 로드됨.
    //   dbQueue  : 서버의 AsyncDBQueue
    //   spBatch  : 로드 요청 배치(shared_ptr). worker 람다에 by-value 복사 캡처.
    //   dbType   : 대상 DB 종류(Account / Game). 한 Load = 한 DB(한 트랜잭션/한 커넥션)라 배치 전체가 이 DB로 간다.
    //   dbIndex  : Game 샤드 인덱스(=game_db_index). Account 면 무시(0).
    //   resume   : 후속작업 재개 executor (보통 Stage 의 GetResumeExecutor() 또는 IOCP)
    static db::AwaitableCoTask<DbLoadResult> Load(db::AsyncDBQueue& dbQueue, const std::shared_ptr<DbLoadBatch>& spBatch, db::EDBType dbType, int dbIndex, db::IResumeExecutor* resume);
};

} // namespace db
