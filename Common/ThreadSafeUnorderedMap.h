#pragma once

#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <functional>
#include <vector>

// ThreadSafeUnorderedMap
//
// thread-safe unordered_map 래퍼.
// TMutex에 std::mutex 또는 std::shared_mutex를 지정한다.
//
// std::shared_mutex  : 읽기가 많은 경우. 읽기 동시 접근 허용. (기본 권장)
// std::mutex         : 읽기/쓰기 빈도가 비슷하거나 map 크기가 작은 경우.
//
// using:
//   SharedThreadSafeUnorderedMap<K,V>     : shared_mutex 버전
//   ExclusiveThreadSafeUnorderedMap<K,V>  : mutex 버전

template<typename TKey, typename TValue, typename TMutex = std::shared_mutex>
class ThreadSafeUnorderedMap
{
    // shared_mutex일 때는 읽기에 shared_lock, 아닐 때는 unique_lock 사용
    using ReadLock  = std::conditional_t<std::is_same_v<TMutex, std::shared_mutex>,
                                         std::shared_lock<std::shared_mutex>,
                                         std::unique_lock<TMutex>>;
    using WriteLock = std::unique_lock<TMutex>;

public:
    // ── 쓰기 ─────────────────────────────────────────────────────────────

    void Insert(const TKey& key, const TValue& value)
    {
        WriteLock lock(m_mutex);
        m_map[key] = value;
    }

    // 삭제. 삭제되었으면 true 반환
    bool Erase(const TKey& key)
    {
        WriteLock lock(m_mutex);
        return m_map.erase(key) > 0;
    }

    // 찾아서 꺼내고 삭제. 찾았으면 true 반환
    bool EraseAndGet(const TKey& key, TValue& outValue)
    {
        WriteLock lock(m_mutex);
        auto iter = m_map.find(key);
        if (iter == m_map.end())
            return false;
        outValue = iter->second;
        m_map.erase(iter);
        return true;
    }

    void Clear()
    {
        WriteLock lock(m_mutex);
        m_map.clear();
    }

    // ── 읽기 ─────────────────────────────────────────────────────────────

    // 찾으면 값을 복사해서 outValue에 넣고 true 반환
    bool Find(const TKey& key, TValue& outValue) const
    {
        ReadLock lock(m_mutex);
        auto iter = m_map.find(key);
        if (iter == m_map.end())
            return false;
        outValue = iter->second;
        return true;
    }

    bool Contains(const TKey& key) const
    {
        ReadLock lock(m_mutex);
        return m_map.contains(key);
    }

    bool Empty() const
    {
        ReadLock lock(m_mutex);
        return m_map.empty();
    }

    size_t Size() const
    {
        ReadLock lock(m_mutex);
        return m_map.size();
    }

    // 전체 순회: 콜백 안에서 lock이 유지됨
    // 콜백 안에서 Insert/Erase 등 쓰기 함수를 호출하면 데드락 발생하므로 주의
    void ForEach(std::function<void(const TKey&, const TValue&)> func) const
    {
        ReadLock lock(m_mutex);
        for (const auto& [key, value] : m_map)
            func(key, value);
    }

    // 조건에 맞는 key 목록 반환 (순회 후 lock 해제, 이후 개별 처리 용도)
    std::vector<TKey> CollectKeys(std::function<bool(const TKey&, const TValue&)> pred) const
    {
        ReadLock lock(m_mutex);
        std::vector<TKey> keys;
        for (const auto& [key, value] : m_map)
        {
            if (pred(key, value))
                keys.push_back(key);
        }
        return keys;
    }

private:
    mutable TMutex                      m_mutex;
    std::unordered_map<TKey, TValue>    m_map;
};

// ── using ─────────────────────────────────────────────────────────────

// 읽기가 많은 경우 (기본 권장)
template<typename TKey, typename TValue>
using SharedThreadSafeUnorderedMap = ThreadSafeUnorderedMap<TKey, TValue, std::shared_mutex>;

// 읽기/쓰기 빈도가 비슷하거나 map 크기가 작은 경우
template<typename TKey, typename TValue>
using ExclusiveThreadSafeUnorderedMap = ThreadSafeUnorderedMap<TKey, TValue, std::mutex>;
