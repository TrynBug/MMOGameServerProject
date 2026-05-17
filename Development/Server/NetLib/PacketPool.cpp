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

    Packet* pPacket = nullptr;
    {
        std::lock_guard<std::mutex> lock(bucket->mtx);
        if (!bucket->freeList.empty())
        {
            pPacket = bucket->freeList.back();
            bucket->freeList.pop_back();
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

    // custom deleter: PacketPool에 반납
    PacketPool* self = this;
    return PacketPtr(pPacket, [self, bucket](Packet* p)
    {
        self->returnToPool(p, bucket);
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

// custom deleter: Packet을 PacketPool에 반납
void PacketPool::returnToPool(Packet* pPacket, Bucket* pBucket)
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

    std::lock_guard<std::mutex> lock(pBucket->mtx);
    pBucket->freeList.push_back(pPacket);
}

// 모든 버핏, 패킷 파괴
void PacketPool::shutdown()
{
    if (!m_bInitialized.exchange(false))
    {
        return;
    }

    for (Bucket* pBucket : m_buckets)
    {
        {
            std::lock_guard<std::mutex> lock(pBucket->mtx);
            for (Packet* pPacket : pBucket->freeList)
            {
                delete pPacket;
            }
            pBucket->freeList.clear();
        }

        delete pBucket;
    }
    m_buckets.clear();
}

} // namespace netlib
