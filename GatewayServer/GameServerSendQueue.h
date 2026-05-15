#pragma once

// 게이트웨이서버에서 게임서버로 보내는 batch 패킷 포맷 설명 (패킷타입은 SERVER_PACKET_ID_RELAY_BATCH_NTF):
// [ netlib::PacketHeader ][ RelayBatchHeader ][ <relay패킷> 또는 <서버간패킷> 여러개 ]
//   - <relay패킷> : [ RelayItemHeader ][ 원본 클라패킷 전체 (netlib::PacketHeader + payload) ]
//   - <서버간패킷> : [ RelayItemHeader ][ 서버간패킷 전체 (netlib::PacketHeader + payload) ]
//
// 게임서버는 batch 패킷을 받았을 때 RelayBatchHeader로 batch 내의 패킷수를 알아내고, RelayItemHeader 로 각 패킷이 relay패킷(클라패킷)인지 서버간패킷인지 알아낸다.

enum class EBatchItemType : uint8
{
    Relay = 0,   // 클라이언트 패킷
    Server = 1   // 서버간 패킷
};

#pragma pack(push, 1)
// batch 내의 패킷수를 알려주는 헤더
struct BatchHeader
{
    uint16 itemCount = 0;  // batch 내의 패킷수
};

// batch 내에서 각 패킷데이터(PacketHeader + payload) 앞에 입력되는 헤더
struct BatchItemHeader
{
    EBatchItemType batchItemType = EBatchItemType::Relay;  // 패킷타입
    int64 userId = 0;  // 클라패킷일 경우 패킷 대상 유저ID
};
#pragma pack(pop)

static_assert(sizeof(BatchHeader) == 2);
static_assert(sizeof(BatchItemHeader) == 9);


// GameServerSendQueue는 게임서버 1개에 보낼 패킷을 모아 보내는 기능을 제공합니다. 게임서버에 보내는 모든 패킷은 여기에 모았다가 전송합니다.
// 게이트웨이서버에는 게임서버연결 1개당 1개의 GameServerSendQueue가 존재합니다.
// - relay 패킷 (클라패킷을 게임서버로 그대로 전달): EnqueueRelay
// - 서버간 통신 서버 패킷: EnqueueServer
// - Send 완료 시 OnSendComplete가 호출되고, 남은 패킷 있으면 즉시 전송합니다.
// - 한 시점에 WSASend 요청이 1개만 존재합니다. (m_bSending atomic)
class GameServerSendQueue
{
public:
    struct QueueItem
    {
        EBatchItemType    type;
        int64             userId;     // type == Relay 일 때만 사용
        netlib::PacketPtr spPacket;
    };

    // relay 패킷 적재 (thread-safe)
    void EnqueueRelay(int64 userId, const netlib::PacketPtr& spPacket, const netlib::ISessionPtr& spSession, netlib::PacketPool& pool, int32 maxPacketSize);

    // 서버간 패킷 적재 (thread-safe)
    void EnqueueServer(const netlib::PacketPtr& spPacket, const netlib::ISessionPtr& spSession, netlib::PacketPool& pool, int32 maxPacketSize);

    // Send 완료 시 IOCP Worker 스레드에서 호출 (thread-safe)
    void OnSendComplete(const netlib::ISessionPtr& spSession, netlib::PacketPool& pool, int32 maxPacketSize);

private:
    // m_bSending 이 false 일 때만 패킷 모아서 Send (thread-safe)
    void trySend(const netlib::ISessionPtr& spSession, netlib::PacketPool& pool, int32 maxPacketSize);

private:
    std::mutex              m_mutex;
    std::vector<QueueItem>  m_collectingQueue;
    std::vector<QueueItem>  m_sendingQueue;
    std::atomic<bool>       m_bSending{ false };
};

using GameServerSendQueuePtr = std::shared_ptr<GameServerSendQueue>;
using GameServerSendQueueWPtr = std::weak_ptr<GameServerSendQueue>;
using GameServerSendQueueUPtr = std::unique_ptr<GameServerSendQueue>;