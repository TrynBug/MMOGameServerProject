#pragma once

#include "pch.h"

namespace packet
{

// protobuf message를 버퍼에 직렬화/역직렬화하는 기능 제공 클래스
class ProtoSerializer
{
public:
    // protobuf message를 버퍼에 직렬화
    template<typename TMsg>
    static netlib::PacketPtr Serialize(uint16 packetType, const TMsg& msg)
    {
        int32 payloadSize = static_cast<int32>(msg.ByteSizeLong());
        int32 totalSize   = static_cast<int32>(sizeof(netlib::PacketHeader)) + payloadSize;

        auto spPacket = std::make_shared<netlib::Packet>(totalSize);

        if (payloadSize > 0)
        {
            uint8* pPayload = spPacket->GetPayload();
            if (!msg.SerializeToArray(pPayload, payloadSize))
            {
                return nullptr;
            }
        }

        spPacket->SetHeader(
            static_cast<uint16>(totalSize),
            packetType,
            netlib::PacketFlags::None
        );

        return spPacket;
    }

    // 버퍼를 protobuf message로 역직렬화
    template<typename TMsg>
    static bool Deserialize(const netlib::Packet& packet, TMsg& outMsg)
    {
        int32 payloadSize = packet.GetPayloadSize();
        if (payloadSize <= 0)
        {
            return true;  // 빈 메시지는 정상
        }

        return outMsg.ParseFromArray(packet.GetPayload(), payloadSize);
    }
};

} // namespace packet
