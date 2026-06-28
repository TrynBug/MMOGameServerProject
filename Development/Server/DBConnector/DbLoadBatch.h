#pragma once

#include "pch.h"
#include "DbTables.h"
#include "ProtoJsonSerializer.h"   // packet::ProtoJsonSerializer::FromJson (parse 클로저용)

#include <functional>

// ─────────────────────────────────────────────────────────────────────────────
// DbLoadBatch — 한 번에 로드할 여러 테이블의 SELECT 요청을 모으는 배치(쓰기측 DbSaveBatch 의 읽기 대칭).
//
// - **항상 make_shared 로 생성**해서 DbLoadExecutor 에 넘긴다.
// - Load<T>/LoadMany<T> 는 **고정 시그니처** (accountId, characterId) — 둘 다 받고,
//   어느 키로 WHERE 를 거는지는 DbTable<T>::LoadKey 가 고른다(호출부는 키 구성 몰라도 됨).
// - 각 요청은 **타입 T 를 가둔 parse 클로저**를 들고 다닌다 → 실행기는 타입을 몰라도
//   워커 스레드에서 한 행(JSON)을 proto 로 역직렬화할 수 있다.
// ─────────────────────────────────────────────────────────────────────────────

namespace db
{

class DbLoadBatch
{
public:
    // 단일 로드(≤1행 기대). 예) Load<Currency>(accountId, characterId)
    template<class T>
    void Load(int64_t accountId, int64_t characterId)
    {
        add<T>(accountId, characterId, /*many*/ false);
    }

    // 목록 로드(N행). 예) LoadMany<Item>(accountId, characterId)
    template<class T>
    void LoadMany(int64_t accountId, int64_t characterId)
    {
        add<T>(accountId, characterId, /*many*/ true);
    }

    bool Empty() const { return m_requests.empty(); }

public:
    // 실행기(DbLoadExecutor)가 읽는 자료구조.
    struct Request
    {
        EDbTable    table;
        const char* tableName;   // SELECT 대상 테이블(DbTable<T>::kInfo.name)
        const char* keyCol;      // WHERE 컬럼(DbTable<T>::kLoadKeyCol)
        int64_t     keyValue;    // WHERE 값(DbTable<T>::LoadKey 가 acc/char 중 선택)
        bool        many;        // false=단일(≤1) / true=목록
        // 워커 스레드에서 한 행(JSON)을 proto 로 파싱한다(타입 T 를 클로저에 가둠). ok=false 면 파싱 실패.
        std::function<std::shared_ptr<google::protobuf::Message>(const std::string& json, bool& ok)> parse;
    };
    const std::vector<Request>& Requests() const { return m_requests; }

private:
    template<class T>
    void add(int64_t accountId, int64_t characterId, bool many)
    {
        Request request;
        request.table     = DbTable<T>::kType;
        request.tableName = DbTable<T>::kInfo.name;
        request.keyCol    = DbTable<T>::kLoadKeyCol;
        request.keyValue  = DbTable<T>::LoadKey(accountId, characterId);   // ← trait 가 키 선택
        request.many      = many;
        request.parse = [](const std::string& json, bool& ok) -> std::shared_ptr<google::protobuf::Message>
        {
            auto message = std::make_shared<T>();
            ok = packet::ProtoJsonSerializer::FromJson(json, *message);
            return message;
        };
        m_requests.push_back(std::move(request));
    }

    std::vector<Request> m_requests;
};

} // namespace db
