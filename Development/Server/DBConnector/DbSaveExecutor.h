#pragma once

#include "pch.h"
#include "DbSaveBatch.h"
#include "AsyncDBQueue.h"   // db::AsyncDBQueue, db::DBTransaction, db::DBResultAwaitable, db::IResumeExecutor

// ─────────────────────────────────────────────────────────────────────────────
// DbSaveExecutor — DbSaveBatch 를 한 샤드 단일 트랜잭션으로 저장한다.
//
// - 테이블은 EDbTable 선언순(락 순서), 같은 테이블 다중 행은 PK 오름차순(RowMap 순회)으로 처리(데드락 예방).
// - 테이블별로 멀티행 INSERT ... ON DUPLICATE KEY UPDATE + DELETE(단일 PK는 IN, 복합 PK는 row-constructor IN).
// - **직렬화는 여기(write 직전, worker 스레드)** 에서 재사용 버퍼로 한다.
// - 본문은 worker 스레드에서 재시도 replay 되므로 순수 DB 작업만 한다(배치는 shared_ptr 공유).
// ─────────────────────────────────────────────────────────────────────────────

namespace db
{

class DbSaveExecutor
{
public:
    // co_await 으로 결과를 기다린다. result.success==true 면 커밋됨.
    //   dbq        : 서버의 AsyncDBQueue
    //   spBatch    : 저장할 배치(shared_ptr). worker 람다에 by-value 복사 캡처 → DB 완료까지 수명 보장.
    //   shardIndex : game_db_index
    //   resume     : 후속작업 재개 executor (보통 Stage 의 GetResumeExecutor())
    static db::DBResultAwaitable Save(db::AsyncDBQueue& dbQueue, const std::shared_ptr<DbSaveBatch>& spBatch, int shardIndex, db::IResumeExecutor* resume);
};

} // namespace db
