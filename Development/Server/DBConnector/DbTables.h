#pragma once

#include "pch.h"
#include "DBTypes.h"   // db::DBParam

// 게임 데이터 구조체(proto) — 이 레지스트리가 다루는 테이블 타입.
#include "Generated/DataStructures/character.pb.h"
#include "Generated/DataStructures/currency.pb.h"
#include "Generated/DataStructures/item.pb.h"
#include "Generated/DataStructures/account_currency.pb.h"
#include "Generated/DataStructures/account.pb.h"

// ─────────────────────────────────────────────────────────────────────────────
// DbTables — DB 테이블 레지스트리
//
// 테이블 정체성을 3가지로 표현한다:
//   1) EDbTable enum    : 런타임 식별자. 배치 맵 키 / 락 순서 / 로깅 기준.
//   2) proto 타입       : 컴파일타임 식별자 + 직렬화 단위(DataStructures::*).
//   3) DbTable<T> trait : 둘을 잇고(kType), 테이블 메타(kInfo)와 키 추출(IdColumns)을 한 곳에 담는다.
//
// 테이블명/PK/컬럼 메타(DbTableInfo)는 각 DbTable<T>::kInfo "한 곳"에만 작성한다.
// GetDbTableInfo(enum) 카탈로그는 그 kInfo 들을 enum 순서로 가리키기만 한다(중복 작성 없음).
//
// ※ 자동생성 대상 파일 — 압축하지 말고 테이블마다 모든 정보를 명시적으로 늘어놓는다.
// ─────────────────────────────────────────────────────────────────────────────

namespace db
{

// 런타임 테이블 식별자.
// *선언 순서 = 한 트랜잭션 안에서의 테이블 락 순서*(데드락 예방). 신중히 정한다.
enum class EDbTable : uint16_t
{
    Character = 0,
    Currency,
    Item,
    AccountCurrency,
    Account,            // AccountDB 의 Accounts 테이블(로드 전용). 게임(GameDB) 트랜잭션과 섞이지 않음(다른 DB).
    Count               // sentinel
};

// 테이블 메타. SQL 빌드/로깅이 이걸 참조. 각 DbTable<T>::kInfo 에 작성한다.
struct DbTableInfo
{
    const char* name;        // 테이블명
    const char* pkCols;      // PK 컬럼 목록(DELETE WHERE / 중복키 식별). 단일="item_id", 복합="account_id, slot"
    int         pkColCount;  // PK 컬럼 수. 키 벡터의 앞 N개가 PK (insertCols 도 PK를 맨 앞에 둔다)
    const char* insertCols;  // INSERT 컬럼 목록(키들 + data, 키 순서는 DbTable<T>::IdColumns 와 일치)
    const char* loadKeyCol;  // 로드 시 WHERE 를 거는 컬럼(소유자 스코프). 값은 DbTable<T>::LoadKey 가 고름. 예) Characters=account_id, Item=character_id
};

// proto 타입 → 테이블 trait. 미등록 타입을 배치에 넣으면 컴파일 에러(누락 방지).
//   kInfo  : 이 테이블의 모든 스키마 메타(이름/PK/컬럼) 단일 출처.
//   IdColumns() : **고정 시그니처** (const T& msg, accountId, characterId). 호출부는 항상 셋 다 넘기고,
//            어떤 키가 필요한지는 각 테이블이 알아서 고른다(호출부가 키 구성을 몰라도 됨).
//            반환은 kInfo.insertCols 의 키 컬럼(data 제외)을 그 순서대로. 자기 PK는 proto에서, 소유자
//            식별자(account/character)는 인자에서 가져온다.
//   [주의] IdColumns() 반환 순서/개수는 kInfo.insertCols 의 키 컬럼과 일치(같은 trait 안에서 함께 관리).
//
//   ── 로드(읽기) 측 ── (DbLoadBatch/DbLoadExecutor 가 사용)
//   kInfo.loadKeyCol : 이 테이블을 로드할 때 WHERE 를 거는 컬럼(소유자 스코프). 예) Characters=account_id, Item=character_id.
//   LoadKey()        : **고정 시그니처** (accountId, characterId). 둘 다 받고, 어느 값으로 loadKeyCol 에 WHERE 를
//                      걸지 고른다(호출부는 키 구성 몰라도 됨). 쓰기와 동일 철학(컬럼명=kInfo, 값=함수).
template<class T> struct DbTable;   // primary template (정의 없음)

template<> struct DbTable<DataStructures::Character>
{
    static constexpr EDbTable    kType = EDbTable::Character;
    static constexpr DbTableInfo kInfo = { "Characters", "character_id", 1, "character_id, account_id, data", /*loadKeyCol*/ "account_id" };
    static std::vector<db::DBParam> IdColumns(const DataStructures::Character& msg, int64_t accountId, int64_t /*characterId*/)
    {
        return {
            msg.character_id(),   // character_id (PK, proto)
            accountId,            // account_id
        };
    }
    // 로드: 계정의 캐릭터 목록 → account_id(=kInfo.loadKeyCol) 로 조회.
    static int64_t LoadKey(int64_t accountId, int64_t /*characterId*/) { return accountId; }
};

template<> struct DbTable<DataStructures::Currency>
{
    static constexpr EDbTable    kType = EDbTable::Currency;
    static constexpr DbTableInfo kInfo = { "Currency", "character_id", 1, "character_id, account_id, data", /*loadKeyCol*/ "character_id" };
    static std::vector<db::DBParam> IdColumns(const DataStructures::Currency& /*msg*/, int64_t accountId, int64_t characterId)
    {
        return {
            characterId,          // character_id (PK)
            accountId,            // account_id
        };
    }
    // 로드: 캐릭터의 재화 → character_id(=kInfo.loadKeyCol) 로 조회.
    static int64_t LoadKey(int64_t /*accountId*/, int64_t characterId) { return characterId; }
};

template<> struct DbTable<DataStructures::Item>
{
    static constexpr EDbTable    kType = EDbTable::Item;
    static constexpr DbTableInfo kInfo = { "Item", "item_id", 1, "item_id, character_id, account_id, data", /*loadKeyCol*/ "character_id" };
    static std::vector<db::DBParam> IdColumns(const DataStructures::Item& msg, int64_t accountId, int64_t characterId)
    {
        return {
            msg.item_id(),        // item_id (PK, proto)
            characterId,          // character_id
            accountId,            // account_id
        };
    }
    // 로드: 캐릭터의 인벤토리 → character_id(=kInfo.loadKeyCol) 로 조회.
    static int64_t LoadKey(int64_t /*accountId*/, int64_t characterId) { return characterId; }
};

template<> struct DbTable<DataStructures::AccountCurrency>
{
    static constexpr EDbTable    kType = EDbTable::AccountCurrency;
    static constexpr DbTableInfo kInfo = { "AccountCurrency", "account_id", 1, "account_id, data", /*loadKeyCol*/ "account_id" };
    static std::vector<db::DBParam> IdColumns(const DataStructures::AccountCurrency& /*msg*/, int64_t accountId, int64_t /*characterId*/)
    {
        return {
            accountId,            // account_id (PK)
        };
    }
    // 로드: 계정 재화 → account_id(=kInfo.loadKeyCol) 로 조회.
    static int64_t LoadKey(int64_t accountId, int64_t /*characterId*/) { return accountId; }
};

// Accounts 는 **로드 전용**(이 레이어로 저장 안 함 — login_name 유니크 인덱스 등 관계형 컬럼이 있어 별도 처리).
// IdColumns 를 정의하지 않으므로 Upsert<Account> 는 컴파일 에러(저장 차단). 이 테이블만 AccountDB 에 있음.
template<> struct DbTable<DataStructures::Account>
{
    static constexpr EDbTable    kType = EDbTable::Account;
    static constexpr DbTableInfo kInfo = { "Accounts", "account_id", 1, "account_id, data", /*loadKeyCol*/ "account_id" };
    // 로드: 계정 → account_id(=kInfo.loadKeyCol) 로 조회.
    static int64_t LoadKey(int64_t accountId, int64_t /*characterId*/) { return accountId; }
};

// enum → 테이블 메타. 각 trait의 kInfo 를 enum 순서로 가리킨다(메타는 trait에 단일 작성).
// 배열 인덱스 = EDbTable 값(선언 순서와 반드시 일치).
inline const DbTableInfo& GetDbTableInfo(EDbTable t)
{
    static const DbTableInfo* const kInfos[static_cast<size_t>(EDbTable::Count)] =
    {
        &DbTable<DataStructures::Character>::kInfo,
        &DbTable<DataStructures::Currency>::kInfo,
        &DbTable<DataStructures::Item>::kInfo,
        &DbTable<DataStructures::AccountCurrency>::kInfo,
        &DbTable<DataStructures::Account>::kInfo,
    };
    return *kInfos[static_cast<size_t>(t)];
}

} // namespace db
