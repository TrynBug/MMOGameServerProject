#pragma once

#include "pch.h"

namespace serverbase
{

// 패킷ID에 해당하는 핸들러 함수를 등록하고 호출하는 기능을 제공하는 클래스
class PacketDispatcher
{
public:
    using PacketHandler = std::function<void(const netlib::ISessionPtr&, const netlib::PacketPtr&)>;

    // 패킷ID와 핸들러 함수 등록
    template<typename TMsg>
    void Register(int packetId, std::function<void(const netlib::ISessionPtr&, const TMsg&)> handler)
    {
        m_handlers[static_cast<uint16>(packetId)] =
            [handler](const netlib::ISessionPtr& spSession, const netlib::PacketPtr& spPacket)
            {
                TMsg msg;
                if (!packet::ProtoSerializer::Deserialize(spPacket->GetPayload(), spPacket->GetPayloadSize(), msg))
                {
                    // 역직렬화 실패: 미등록 핸들러와 달리 세션을 끊는 것이 안전하다.
                    // 호출자(서버)가 판단할 수 있도록 세션 참조를 넘긴 채 그냥 반환한다.
                    // 실제 끊김 처리는 서버별 정책에 따라 UnknownHandler 또는 상위 코드에서 담당.
                    return;
                }
                handler(spSession, msg);
            };
    }

    // 패킷ID에 해당하는 핸들러 호출
    void Dispatch(const netlib::ISessionPtr& spSession, const netlib::PacketPtr& spPacket) const
    {
        const uint16 packetType = spPacket->GetHeader()->type;
        auto iter = m_handlers.find(packetType);
        if (iter != m_handlers.end())
        {
            iter->second(spSession, spPacket);
            return;
        }

        if (m_unknownHandler)
            m_unknownHandler(spSession, spPacket);
    }

    // 미등록 패킷 수신 시 호출될 핸들러 등록 (로그, 연결 끊기 등)
    void SetUnknownPacketHandler(PacketHandler unknownHandler)
    {
        m_unknownHandler = std::move(unknownHandler);
    }

private:
    std::unordered_map<uint16, PacketHandler> m_handlers;
    PacketHandler m_unknownHandler;
};

} // namespace serverbase
