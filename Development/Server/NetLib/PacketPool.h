#pragma once

#include "Types.h"
#include "Packet.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace netlib
{

// 패킷버퍼 풀
// 
// 다양한 크기의 버퍼가 준비되어 있고, 요청 크기에 맞는 버퍼를 버킷에서 꺼내서 할당해준다.
// 1개의 버킷은 같은 크기의 버퍼를 freeList로 관리한다.
// 예를들면 initPacketSize = 512 라면, 각각의 버킷은 512, 1024, 2048, 4096, ..., maxPacketSize 크기의 버퍼를 관리한다.
// 이 때 사용자가 1000바이트 크기의 버퍼를 요청하면 1024버킷에서 할당해준다.
// 
// Packet 객체는 스마트포인터로 관리되며, Packet 객체에 커스텀 deleter를 등록해두었기 때문에 소멸될 때 버킷의 freeList로 자동으로 되돌아간다.
class PacketPool
{
public:
    PacketPool();
    ~PacketPool();

    PacketPool(const PacketPool&)            = delete;
    PacketPool& operator=(const PacketPool&) = delete;

    void Initialize(int32 initPacketSize, int32 maxPacketSize);

    // size에 맞는 패킷버퍼 할당. 주의할 점: size는 header크기 + payload크기 를 입력해야 한다.
    PacketPtr Alloc(int32 size);

    // 기본 크기(initPacketSize)의 패킷버퍼 할당
    PacketPtr Alloc() { return Alloc(m_initPacketSize); }

private:
    struct Bucket
    {
        int32                 capacity = 0;    // 이 버킷이 가지는 Packet의 버퍼크기
        std::mutex            mtx;
        std::vector<Packet*>  freeList;        // 재사용 대기중인 패킷 포인터
    };

    // 크기에 맞는 버킷을 찾는다. 못 찾으면 nullptr.
    Bucket* findBucketFor(int32 size);

    // shared_ptr<Packet>의 커스텀 deleter. Packet을 버킷 freeList로 돌려보냄.
    void returnToPool(Packet* pkt, Bucket* bucket);

    void shutdown();

private:
    int32                        m_initPacketSize = 0;
    int32                        m_maxPacketSize  = 0;
    std::vector<Bucket*>         m_buckets;               // bucket.capacity가 낮은 순서대로 입력됨
    std::atomic<bool>            m_bInitialized   { false };
};

} // namespace netlib
