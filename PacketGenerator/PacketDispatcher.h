#pragma once

#include "pch.h"
#include "ProtoSerializer.h"

namespace packet
{

// 패킷ID에 해당하는 핸들러 함수 등록 기능을 제공하는 클래스
class PacketDispatcher
{
public:
    // 패킷ID와 핸들러 함수 등록
    template<typename TPacketId, typename TMsg>
    void Register(TPacketId packetId, std::function<void(TMsg&&)> handler)
    {
        m_handlers[static_cast<uint16>(packetId)] =
            [handler](const netlib::Packet& packet)
            {
                TMsg msg;
                if (!ProtoSerializer::Deserialize(packet, msg))
                {
                    // 여기에 역직렬화 실패 오류메시지 등록 필요
                    return;
                }
                handler(std::move(msg));
            };
    }

    // 패킷ID에 해당하는 핸들러 호출
    void Dispatch(const netlib::Packet& packet) const
    {
        uint16 packetId = packet.GetHeader()->type;
        auto iter = m_handlers.find(packetId);
        if (iter != m_handlers.end())
        {
            iter->second(packet);
            return;
        }

		// 미등록 패킷 핸들러 호출 (디버깅, 로그 용도)
        if (m_unknownHandler)
        {
            m_unknownHandler(packet);
        }
    }

    // 미등록 패킷 핸들러 등록 (디버깅, 로그 용도)
    void SetUnknownPacketHandler(std::function<void(const netlib::Packet&)> handler)
    {
        m_unknownHandler = std::move(handler);
    }

private:
    std::unordered_map<uint16, std::function<void(const netlib::Packet&)>> m_handlers;        // Key=PacketId, Value=Handler
	std::function<void(const netlib::Packet&)>                             m_unknownHandler;  // 미등록 패킷 핸들러
};

} // namespace packet
