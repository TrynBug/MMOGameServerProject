#include "pch.h"

#include "PacketHeader.h"
#include "Packet.h"

#include "PacketPool.h"

namespace netlib
{

namespace
{
// 현재 스레드가 사용할 freelist 샤드 인덱스. 스레드별로 고정(thread_local 캐시).
size_t poolShardIndex()
{
    thread_local const size_t idx = std::hash<std::thread::id>{}(std::this_thread::get_id()) % kPacketPoolShardCount;
    return idx;
}
}

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
        }
    }

    if (pPacket == nullptr)
    {
        pPacket = new Packet(bucket->capacity);
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
