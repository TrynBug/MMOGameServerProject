#include "pch.h"

#include "PacketHeader.h"
#include "Packet.h"

#include "PacketPool.h"

namespace netlib
{

PacketPool::PacketPool()
{
}

PacketPool::~PacketPool()
{
    shutdown();
}

void PacketPool::Initialize(int32 initPacketSize, int32 maxPacketSize)
{
    if (m_bInitialized.exchange(true))
    {
        return;
    }

    m_initPacketSize = initPacketSize;
    m_maxPacketSize  = maxPacketSize;

    // 크기를 2배씩 증가시키며 버킷 생성.
    int32 size = initPacketSize;
    while (size < maxPacketSize)
    {
        Bucket* pBucket  = new Bucket();
        pBucket->capacity = size;
        m_buckets.push_back(pBucket);
        size *= 2;
    }

    // 마지막 maxPacketSize 버킷 생성
    Bucket* pLastBucket  = new Bucket();
    pLastBucket->capacity = maxPacketSize;
    m_buckets.push_back(pLastBucket);
}


// 현재 스레드가 사용할 freelist 샤드 인덱스. 스레드별로 고정(thread_local 캐시).
// thread id 해시는 충돌 가능성이 있어, 단조 증가 m_nextShard 로 스레드를 라운드로빈 배정한다.
// (풀을 처음 쓰는 시점에 다음 번호를 한 번 받아 % K 로 고정 → ≤ K 스레드까지 서로 다른 shard 보장)
size_t PacketPool::poolShardIndex()
{
    thread_local const size_t idx = m_nextShard.fetch_add(1, std::memory_order_relaxed) % kPacketPoolShardCount;
    return idx;
}

// size에 맞는 패킷버퍼 할당. 주의할 점: size는 header크기 + payload크기 를 입력해야 한다.
PacketPtr PacketPool::Alloc(int32 size)
{
    // 버킷 찾기
    Bucket* bucket = findBucketFor(size);
    if (bucket == nullptr)
    {
        // size가 너무 커서 버킷을 못찾음
        return nullptr;
    }

    // 자기 샤드부터 시도하고, 비어있으면 다른 샤드를 순회한다(다른 스레드가 반납한 패킷 회수). 모두 비면 new.
    Packet* pPacket = nullptr;
    const size_t base = poolShardIndex();
    for (size_t i = 0; i < kPacketPoolShardCount && pPacket == nullptr; ++i)
    {
        FreeShard& shard = bucket->shards[(base + i) % kPacketPoolShardCount];
        std::lock_guard<std::mutex> lock(shard.mtx);
        if (!shard.freeList.empty())
        {
            pPacket = shard.freeList.back();
            shard.freeList.pop_back();
            ++shard.servedAlloc;   // [통계] 이 shard 가 Alloc 을 충족
        }
        else
        {
            ++shard.emptyProbe;    // [통계] 빈 shard 를 probe (스캔 비용 지표)
        }
    }

    if (pPacket == nullptr)
    {
        pPacket = new Packet(bucket->capacity);

        // [통계] home shard(base) 에 new 발생 기록
        std::lock_guard<std::mutex> lock(bucket->shards[base].mtx);
        ++bucket->shards[base].created;
    }
    else
    {
        pPacket->Reset();
    }

    // custom deleter: alloc 한 스레드의 home shard(base)로 반납한다.
    // 이렇게 하면 이 스레드의 다음 Alloc 이 자기 shard 에서 바로 hit 하여 cross-shard 스캔이 드물어진다.
    PacketPool* self = this;
    return PacketPtr(pPacket, [self, bucket, base](Packet* p)
    {
        self->returnToPool(p, bucket, base);
    });
}

// 크기에 맞는 버핏 찾기
PacketPool::Bucket* PacketPool::findBucketFor(int32 size)
{
    // 오름차순으로 순회하며 size 이상인 버킷을 찾는다.
    for (Bucket* bucket : m_buckets)
    {
        if (bucket->capacity >= size)
        {
            return bucket;
        }
    }
    return nullptr;
}

// custom deleter: Packet을 alloc 했던 스레드의 home shard로 반납
void PacketPool::returnToPool(Packet* pPacket, Bucket* pBucket, size_t shardIndex)
{
    if (pPacket == nullptr)
    {
        return;
    }

    // Shutdown 이후라면 그냥 delete.
    if (!m_bInitialized.load())
    {
        delete pPacket;
        return;
    }

    FreeShard& shard = pBucket->shards[shardIndex];
    std::lock_guard<std::mutex> lock(shard.mtx);
    shard.freeList.push_back(pPacket);
    ++shard.returned;   // [통계] 이 shard 로 반납됨
}

// 버킷별 통계 스냅샷. 각 shard 를 잠깐 잠그며 카운터·보유수를 수집한다(진단용).
std::vector<PacketPool::BucketStats> PacketPool::GetStats() const
{
    std::vector<BucketStats> out;
    out.reserve(m_buckets.size());

    for (Bucket* pBucket : m_buckets)
    {
        BucketStats s;
        s.capacity = pBucket->capacity;

        for (size_t i = 0; i < kPacketPoolShardCount; ++i)
        {
            FreeShard& shard = pBucket->shards[i];
            std::lock_guard<std::mutex> lock(shard.mtx);

            const int32 held = static_cast<int32>(shard.freeList.size());
            s.shardHeld[i]   = held;
            s.held          += held;
            s.allocCount    += shard.servedAlloc + shard.created;
            s.freeCount     += shard.returned;
            s.newCount      += shard.created;
            s.scanMissCount += shard.emptyProbe;
        }

        out.push_back(s);
    }

    return out;
}

// 모든 버킷, 패킷 파괴
void PacketPool::shutdown()
{
    if (!m_bInitialized.exchange(false))
    {
        return;
    }

    for (Bucket* pBucket : m_buckets)
    {
        for (FreeShard& shard : pBucket->shards)
        {
            std::lock_guard<std::mutex> lock(shard.mtx);
            for (Packet* pPacket : shard.freeList)
            {
                delete pPacket;
            }
            shard.freeList.clear();
        }

        delete pBucket;
    }
    m_buckets.clear();
}

} // namespace netlib
