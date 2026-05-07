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
        m_handlers[static_cast<uint16_t>(packetId)] =
            [handler](const uint8_t* pPayload, int32_t payloadSize)
            {
                TMsg msg;
                if (!ProtoSerializer::Deserialize(pPayload, payloadSize, msg))
                {
                    // 여기에 역직렬화 실패 오류메시지 등록 필요
                    return;
                }
                handler(std::move(msg));
            };
    }

    // 패킷ID에 해당하는 핸들러 호출
    // @packetType : PacketHeader::type
    // @pPayload   : PacketHeader 다음 위치의 포인터
    // @payloadSize: payload 크기
    void Dispatch(uint16_t packetType, const uint8_t* pPayload, int32_t payloadSize) const
    {
        auto iter = m_handlers.find(packetType);
        if (iter != m_handlers.end())
        {
            iter->second(pPayload, payloadSize);
            return;
        }

        // 미등록 패킷 핸들러 호출 (디버깅, 로그 용도)
        if (m_unknownHandler)
        {
            m_unknownHandler(packetType);
        }
    }

    // 미등록 패킷 핸들러 등록 (디버깅, 로그 용도)
    void SetUnknownPacketHandler(std::function<void(uint16_t)> handler)
    {
        m_unknownHandler = std::move(handler);
    }

private:
    std::unordered_map<uint16_t, std::function<void(const uint8_t*, int32_t)>> m_handlers;  // Key=PacketId, Value=Handler
    std::function<void(uint16_t)> m_unknownHandler;  // 미등록 패킷 핸들러
};

} // namespace packet
