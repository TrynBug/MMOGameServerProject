#include "pch.h"
#include "GameServerSendQueue.h"

// relay 패킷 적재 (thread-safe)
void GameServerSendQueue::EnqueueRelay(int64 userId, const netlib::PacketPtr& spPacket, const netlib::ISessionPtr& spSession, netlib::PacketPool& pool, int32 maxPacketSize)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_collectingQueue.push_back({ EBatchItemType::Relay, userId, spPacket });
    }

    trySend(spSession, pool, maxPacketSize);
}

// 서버간 패킷 적재 (thread-safe)
void GameServerSendQueue::EnqueueServer(const netlib::PacketPtr& spPacket, const netlib::ISessionPtr& spSession, netlib::PacketPool& pool, int32 maxPacketSize)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_collectingQueue.push_back({ EBatchItemType::Server, 0, spPacket });
    }

    trySend(spSession, pool, maxPacketSize);
}

// Send 완료 시 IOCP Worker 스레드에서 호출 (thread-safe)
void GameServerSendQueue::OnSendComplete(const netlib::ISessionPtr& spSession, netlib::PacketPool& pool, int32 maxPacketSize)
{
    m_bSending.store(false);
    trySend(spSession, pool, maxPacketSize);
}


// m_bSending 이 false 일 때만 패킷 모아서 Send (thread-safe)
void GameServerSendQueue::trySend(const netlib::ISessionPtr& spSession, netlib::PacketPool& pool, int32 maxPacketSize)
{
    bool expected = false;
    if (!m_bSending.compare_exchange_strong(expected, true))
        return;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_collectingQueue.empty())
        {
            m_bSending.store(false);
            return;
        }

        m_sendingQueue.swap(m_collectingQueue);  // vector의 내부정보만(배열포인터, size, capacity등) swap함
    }

    // 배치 패킷 조립
    // 패킷구조: [ NetLib PacketHeader ][ BatchHeader ][ item... ]
    // item 종류:
    //   - relay 패킷: [ BatchItemHeader ][ 원본 클라패킷 NetLib PacketHeader][ 원본 클라패킷 payload ]
    //   - 서버간 패킷: [ BatchItemHeader ][ 서버간패킷 NetLib PacketHeader][ 서버간패킷 payload ]
    const int32 fixedOverhead = static_cast<int32>(sizeof(netlib::PacketHeader) + sizeof(BatchHeader));
    int32 batchSize = fixedOverhead;
    uint16 itemCount = 0;

    // 필요한 크기 미리 계산
    for (const QueueItem& item : m_sendingQueue)
    {
        int32 itemSize = static_cast<int32>(sizeof(BatchItemHeader)) + item.spPacket->GetTotalSize();

        // maxPacketSize 초과 시 일단 여기까지만 (초과분은 다음 Send 때 처리)
        if (batchSize + itemSize >= maxPacketSize)
            break;

        batchSize += itemSize;
        ++itemCount;
    }

    netlib::PacketPtr spBatchPacket = pool.Alloc(batchSize);
    if (!spBatchPacket)
    {
        // 할당 실패 시 큐에 돌려놓고 포기
        std::lock_guard<std::mutex> lock(m_mutex);
        m_collectingQueue.insert(m_collectingQueue.begin(), m_sendingQueue.begin(), m_sendingQueue.end());
        m_sendingQueue.clear();
        m_bSending.store(false);
        return;
    }

    // 헤더 기록
    spBatchPacket->SetHeader(
        sizeof(netlib::PacketHeader),
        static_cast<uint16>(Common::SERVER_PACKET_ID_RELAY_BATCH_NTF),
        netlib::PacketFlags::None
    );

    // BatchHeader 입력
    BatchHeader batchHeader;
    batchHeader.itemCount = itemCount;
    spBatchPacket->WritePayload(reinterpret_cast<const char*>(&batchHeader), sizeof(BatchHeader));

    // 각 패킷데이터 기록
    uint16 written = 0;
    for (const QueueItem& item : m_sendingQueue)
    {
        if (written >= itemCount)
        {
            // maxPacketSize 초과로 이번 배치에 못 들어간 항목은 큐에 돌려놓음
            std::lock_guard<std::mutex> lock(m_mutex);
            m_collectingQueue.insert(m_collectingQueue.begin(), m_sendingQueue.begin() + written, m_sendingQueue.end());
            break;
        }

        // BatchItemHeader 입력
        BatchItemHeader batchItemHeader;
        batchItemHeader.batchItemType = item.type;
        batchItemHeader.userId = item.userId;
        spBatchPacket->WritePayload(reinterpret_cast<const char*>(&batchItemHeader), sizeof(BatchItemHeader));

        // 원본 패킷내용 입력
        spBatchPacket->WritePayload(reinterpret_cast<const char*>(item.spPacket->GetRawBuffer()), item.spPacket->GetTotalSize());

        ++written;
    }

    m_sendingQueue.clear();

    spSession->Send(spBatchPacket);
}