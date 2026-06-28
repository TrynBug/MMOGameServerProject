#pragma once

#include "pch.h"
#include "DbTables.h"   // EDbTable, DbTable<T>

#include <map>

// ─────────────────────────────────────────────────────────────────────────────
// DbLoadResult — DbLoadExecutor 가 워커 스레드에서 파싱해 채운 결과.
//
// 테이블별로 **이미 역직렬화된 proto(shared_ptr<Message>)** 를 보관한다.
// 게임로직 스레드는 Get<T>()/GetMany<T>() 로 꺼내 쓰기만 한다(JSON 파싱 없음).
// ─────────────────────────────────────────────────────────────────────────────

namespace db
{

class DbLoadResult
{
public:
    bool success = false;

    // 단일 로드(≤1행) 결과. 없으면 nullptr.
    template<class T>
    std::shared_ptr<T> Get() const
    {
        auto found = m_byTable.find(DbTable<T>::kType);
        if (found == m_byTable.end() || found->second.empty())
        {
            return nullptr;
        }
        return std::static_pointer_cast<T>(found->second.front());
    }

    // 목록 로드 결과. 없으면 빈 벡터.
    template<class T>
    std::vector<std::shared_ptr<T>> GetMany() const
    {
        std::vector<std::shared_ptr<T>> out;
        auto found = m_byTable.find(DbTable<T>::kType);
        if (found != m_byTable.end())
        {
            out.reserve(found->second.size());
            for (const auto& message : found->second)
            {
                out.push_back(std::static_pointer_cast<T>(message));
            }
        }
        return out;
    }

    // ── 실행기(DbLoadExecutor)가 워커 스레드에서 채운다 ──
    void Add(EDbTable table, std::shared_ptr<google::protobuf::Message> message)
    {
        m_byTable[table].push_back(std::move(message));
    }
    void Clear()
    {
        m_byTable.clear();
    }

private:
    // 테이블 → 파싱된 proto 들(파싱은 워커 스레드, 소유권은 게임로직 스레드로 이동).
    std::map<EDbTable, std::vector<std::shared_ptr<google::protobuf::Message>>> m_byTable;
};

} // namespace db
