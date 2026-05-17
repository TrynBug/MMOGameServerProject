#include "pch.h"
#include "User.h"

User::User(int64 userId, int32 gatewayId, const std::string& clientIp)
    : m_userId(userId)
    , m_gatewayId(gatewayId)
    , m_clientIp(clientIp)
{
}

void User::EnqueuePacket(netlib::PacketPtr spPacket)
{
    if (!spPacket)
        return;

    std::lock_guard<std::mutex> lock(m_packetQueueMutex);
    m_packetQueue.push_back(std::move(spPacket));
}

void User::DrainPackets(std::vector<netlib::PacketPtr>& outPackets)
{
    std::lock_guard<std::mutex> lock(m_packetQueueMutex);
    outPackets.swap(m_packetQueue);
    // swap 후 m_packetQueue는 비어있는 상태가 됨
}
