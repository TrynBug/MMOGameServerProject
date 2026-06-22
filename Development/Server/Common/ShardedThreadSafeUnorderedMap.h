#pragma once

#include <array>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <functional>
#include <vector>

// ShardedThreadSafeUnorderedMap
//
// ThreadSafeUnorderedMap 과 동일한 인터페이스를 가지는 thread-safe unordered_map 래퍼이지만,
// 내부를 ShardCount 개의 샤드(각각 자기 mutex + unordered_map)로 분할한다.
// key 의 해시로 샤드를 고르므로, 서로 다른 key 에 대한 동시 접근이 서로 다른 락을 잡아
// 단일 mutex 의 캐시라인 경합을 ShardCount 배로 분산한다.
//
// 단일 mutex 버전(ThreadSafeUnorderedMap) 대비 차이점:
//   - 매 패킷마다 Find 가 호출되는 핫 맵(예: GameServer/GatewayServer 의 accountId→User 맵)에서
//     락 경합을 줄이는 용도. 작은/저빈도 맵에는 ThreadSafeUnorderedMap 를 그대로 쓰면 된다.
//   - ForEach/CollectKeys/Size/Empty 는 "전역 단일 스냅샷"이 아니라 "샤드별 스냅샷"이다
//     (샤드를 하나씩 잠그며 순회). 호출 사이에 다른 샤드가 변경될 수 있다. 정리/순회 용도에는 충분.
//
// TMutex 에 std::shared_mutex(기본, 읽기 동시허용) 또는 std::mutex 지정 가능.
template<typename TKey, typename TValue, size_t ShardCount = 32, typename TMutex = std::shared_mutex>
class ShardedThreadSafeUnorderedMap
{
    static_assert(ShardCount > 0, "ShardCount must be > 0");

    using ReadLock  = std::conditional_t<std::is_same_v<TMutex, std::shared_mutex>,
                                         std::shared_lock<std::shared_mutex>,
                                         std::unique_lock<TMutex>>;
    using WriteLock = std::unique_lock<TMutex>;

    struct Shard
    {
        mutable TMutex                   mutex;
        std::unordered_map<TKey, TValue> map;
    };

    // key 가 속한 샤드를 고른다.
    Shard&       shardFor(const TKey& key)       { return m_shards[std::hash<TKey>{}(key) % ShardCount]; }
    const Shard& shardFor(const TKey& key) const { return m_shards[std::hash<TKey>{}(key) % ShardCount]; }

public:
    // ── 쓰기 ─────────────────────────────────────────────────────────────

    void Insert(const TKey& key, const TValue& value)
    {
        Shard& shard = shardFor(key);
        WriteLock lock(shard.mutex);
        shard.map[key] = value;
    }

    // 삭제. 삭제되었으면 true 반환
    bool Erase(const TKey& key)
    {
        Shard& shard = shardFor(key);
        WriteLock lock(shard.mutex);
        return shard.map.erase(key) > 0;
    }

    // 여러개 한번에 삭제. 삭제된 개수 반환 (key 별 해당 샤드 락)
    int Erase(const std::vector<TKey>& keys)
    {
        int deleted = 0;
        for (const TKey& key : keys)
        {
            Shard& shard = shardFor(key);
            WriteLock lock(shard.mutex);
            deleted += (shard.map.erase(key) > 0);
        }
        return deleted;
    }

    // 찾아서 꺼내고 삭제. 찾았으면 true 반환
    bool EraseAndGet(const TKey& key, TValue& outValue)
    {
        Shard& shard = shardFor(key);
        WriteLock lock(shard.mutex);
        auto iter = shard.map.find(key);
        if (iter == shard.map.end())
            return false;
        outValue = iter->second;
        shard.map.erase(iter);
        return true;
    }

    void Clear()
    {
        for (Shard& shard : m_shards)
        {
            WriteLock lock(shard.mutex);
            shard.map.clear();
        }
    }

    // ── 읽기 ─────────────────────────────────────────────────────────────

    // 찾으면 값을 복사해서 outValue에 넣고 true 반환
    bool Find(const TKey& key, TValue& outValue) const
    {
        const Shard& shard = shardFor(key);
        ReadLock lock(shard.mutex);
        auto iter = shard.map.find(key);
        if (iter == shard.map.end())
            return false;
        outValue = iter->second;
        return true;
    }

    bool Contains(const TKey& key) const
    {
        const Shard& shard = shardFor(key);
        ReadLock lock(shard.mutex);
        return shard.map.contains(key);
    }

    bool Empty() const
    {
        for (const Shard& shard : m_shards)
        {
            ReadLock lock(shard.mutex);
            if (!shard.map.empty())
                return false;
        }
        return true;
    }

    size_t Size() const
    {
        size_t total = 0;
        for (const Shard& shard : m_shards)
        {
            ReadLock lock(shard.mutex);
            total += shard.map.size();
        }
        return total;
    }

    // 전체 순회: 샤드를 하나씩 잠그며 콜백 호출 (콜백 동안 해당 샤드 락 유지)
    // 주의: 콜백 안에서 Insert/Erase 등 쓰기 함수를 호출하면 같은 샤드에서 데드락 발생 가능
    void ForEach(std::function<void(const TKey&, const TValue&)> func) const
    {
        for (const Shard& shard : m_shards)
        {
            ReadLock lock(shard.mutex);
            for (const auto& [key, value] : shard.map)
                func(key, value);
        }
    }

    // 조건에 맞는 key 목록 반환 (순회 후 락 해제, 이후 개별 처리 용도)
    std::vector<TKey> CollectKeys(std::function<bool(const TKey&, const TValue&)> pred) const
    {
        std::vector<TKey> keys;
        for (const Shard& shard : m_shards)
        {
            ReadLock lock(shard.mutex);
            for (const auto& [key, value] : shard.map)
            {
                if (pred(key, value))
                    keys.push_back(key);
            }
        }
        return keys;
    }

private:
    std::array<Shard, ShardCount> m_shards;
};
