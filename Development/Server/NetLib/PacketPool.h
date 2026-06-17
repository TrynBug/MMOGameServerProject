#pragma once

#include "Types.h"
#include "Packet.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace netlib
{

// 버킷당 freelist 샤드 수. alloc/free 스레드가 해시로 서로 다른 샤드를 잡아 mutex 경합을 분산한다.
inline constexpr size_t kPacketPoolShardCount = 8;

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

    // ── 통계 ─────────────────────────────────────────────
    // 버킷 1개의 통계 스냅샷.
    struct BucketStats
    {
        int32  capacity      = 0;     // 이 버킷의 버퍼 크기
        uint64 allocCount    = 0;     // 총 Alloc 충족 횟수 (pool hit + new)
        uint64 freeCount     = 0;     // 총 반납(free) 횟수
        uint64 newCount      = 0;     // 풀이 비어 new Packet 한 횟수 (pool miss)
        uint64 scanMissCount = 0;     // Alloc 스캔 중 빈 shard 를 probe 한 횟수 (cross-shard 스캔/비대칭 지표)
        int32  held          = 0;     // 현재 풀에 보유(재사용 대기)중인 Packet 수
        std::array<int32, kPacketPoolShardCount> shardHeld{};  // shard 별 보유 수 (free 분포 확인용)
    };

    // 버킷별 통계 스냅샷을 반환한다. (각 shard 를 잠깐 잠그며 수집 — 진단용, 핫패스 아님)
    std::vector<BucketStats> GetStats() const;

private:
    // 버킷 내 freelist 1개 샤드 (각자 mutex). 스레드 해시로 분산 접근.
    struct FreeShard
    {
        std::mutex            mtx;
        std::vector<Packet*>  freeList;        // 재사용 대기중인 패킷 포인터

        // ── 통계 (모두 mtx 보호 하에서만 갱신/읽기 → 추가 atomic·공유 캐시라인 없음) ──
        uint64 servedAlloc = 0;   // 이 shard 에서 pop 으로 Alloc 을 충족한 횟수
        uint64 returned    = 0;   // 이 shard 로 반납된 횟수 (free)
        uint64 created     = 0;   // 이 shard 가 home 인 new Packet 횟수 (pool miss)
        uint64 emptyProbe  = 0;   // Alloc 스캔 중 이 shard 가 비어 probe 만 한 횟수
    };

    struct Bucket
    {
        int32                                       capacity = 0;    // 이 버킷이 가지는 Packet의 버퍼크기
        std::array<FreeShard, kPacketPoolShardCount> shards;          // freelist 를 샤드로 분할
    };

    // 크기에 맞는 버킷을 찾는다. 못 찾으면 nullptr.
    Bucket* findBucketFor(int32 size);

    // 현재 스레드가 사용할 freelist 샤드 인덱스. 스레드별로 고정(thread_local 캐시).
    // m_nextShard 로 스레드를 라운드로빈 배정한다(≤ kPacketPoolShardCount 스레드까지 서로 다른 shard 보장).
    size_t poolShardIndex();

    // shared_ptr<Packet>의 커스텀 deleter. Packet을 alloc 했던 스레드의 home shard(shardIndex)로 돌려보냄.
    void returnToPool(Packet* pkt, Bucket* bucket, size_t shardIndex);

    void shutdown();

private:
    int32                        m_initPacketSize = 0;
    int32                        m_maxPacketSize  = 0;
    std::vector<Bucket*>         m_buckets;               // bucket.capacity가 낮은 순서대로 입력됨
    std::atomic<bool>            m_bInitialized   { false };
    std::atomic<size_t>          m_nextShard      { 0 };  // 스레드별 shard 인덱스 라운드로빈 배정용
};

} // namespace netlib
