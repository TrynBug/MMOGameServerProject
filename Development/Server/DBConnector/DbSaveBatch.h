#pragma once

#include "pch.h"
#include "DbTables.h"

#include <map>

// ─────────────────────────────────────────────────────────────────────────────
// DbSaveBatch — 한 트랜잭션으로 저장할 여러 테이블의 upsert/delete 를 모으는 배치 컨테이너.
//
// - **항상 make_shared 로 생성**해서 여러 함수가 공유하며 누적한다.
// - 엔트리는 **proto 를 shared_ptr 로 보유**(직렬화 원본 + DB 성공 후 readback/로깅용).
//   직렬화는 DbSaveExecutor 가 write 직전에 한다(여기서 JSON 을 들지 않음).
// - upsert / delete 는 동일 구조로 관리하고 isDelete 로만 구분.
// - 테이블별 행을 **PK 튜플 키의 std::map(RowMap)** 으로 보관:
//     · 같은 PK 재입력 → 자동 중복검출 + 교체(경고 로그). delta 누적 안 함(단일 소유라 통째 교체).
//     · 순회가 PK 오름차순 → 데드락 예방(행 락 순서) 무비용 충족.
// - 테이블 맵은 std::map<EDbTable,..>(enum 선언순) → 테이블 락 순서 일관.
//
// [불변식] 배치에 넣는 proto 는 private copy 여야 하고(live/aliased proto 금지), Save in-flight 동안
//          수정하지 않는다. staging 코루틴은 co_await 에서 suspend 되어 그 사이 수정이 구조적으로 불가능.
// ─────────────────────────────────────────────────────────────────────────────

namespace db
{

class DbSaveBatch
{
public:
    // upsert/delete 예약. **고정 시그니처** — 항상 (proto, accountId, characterId) 를 넘긴다.
    // 어떤 키가 실제로 쓰이는지는 DbTable<T>::Keys 가 알아서 고르므로, 호출부는 키 구성을 몰라도 된다.
    //   예) Upsert(spItem, accountId, characterId) / Delete(spItem, accountId, characterId)
    //       Upsert(spCharacter, accountId, characterId) / Upsert(spAccountCurrency, accountId, characterId)
    template<class T>
    void Upsert(std::shared_ptr<T> msg, int64_t accountId, int64_t characterId)
    {
        auto keys = DbTable<T>::Keys(*msg, accountId, characterId);
        add(DbTable<T>::kType, std::move(msg), std::move(keys), /*isDelete*/ false);
    }

    template<class T>
    void Delete(std::shared_ptr<T> msg, int64_t accountId, int64_t characterId)
    {
        auto keys = DbTable<T>::Keys(*msg, accountId, characterId);
        add(DbTable<T>::kType, std::move(msg), std::move(keys), /*isDelete*/ true);
    }

    bool Empty() const { return m_tables.empty(); }

public:
    // 실행기(DbSaveExecutor)가 읽는 자료구조.
    struct Entry
    {
        std::shared_ptr<google::protobuf::Message> proto;  // 직렬화 원본 + readback/로깅(non-const)
        std::vector<db::DBParam>                   keys;   // 전체 키(insertCols 순서). PK = 앞 pkColCount개.
        bool                                       isDelete = false;
    };
    // PK 튜플 → Entry. 키 정렬 = PK 오름차순, 같은 PK = 교체.
    using RowMap = std::map<std::vector<db::DBParam>, Entry>;

    const std::map<EDbTable, RowMap>& Tables() const { return m_tables; }

private:
    void add(EDbTable t, std::shared_ptr<google::protobuf::Message> proto, std::vector<db::DBParam> keys, bool isDelete)
    {
        const DbTableInfo& info = GetDbTableInfo(t);
        std::vector<db::DBParam> pk(keys.begin(), keys.begin() + info.pkColCount);   // PK 튜플
        RowMap& rows = m_tables[t];
        if (rows.find(pk) != rows.end())   // 같은 PK 중복 입력 → 경고 + 교체(last-wins)
            LOG_WRITE(LogLevel::Warn, std::format("[DbSaveBatch] duplicate key, replacing. table={}", info.name));
        rows[std::move(pk)] = Entry{ std::move(proto), std::move(keys), isDelete };
    }

    std::map<EDbTable, RowMap> m_tables;   // 테이블=enum 선언순(락 순서), 행=PK 오름차순(맵 정렬)
};

} // namespace db
