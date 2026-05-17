#pragma once

#include "pch.h"

namespace packet
{

// protobuf message를 버퍼에 직렬화/역직렬화하는 기능 제공 클래스
class ProtoSerializer
{
public:
    // protobuf message의 직렬화 payload 크기를 반환 (PacketHeader 미포함)
    template<typename TMsg>
    static int GetPayloadSize(const TMsg& msg)
    {
        return static_cast<int32_t>(msg.ByteSizeLong());
    }

    // protobuf message를 payload 버퍼에 직렬화
    // @pPayload : PacketHeader 다음 위치의 포인터
    // @bufferSize : pPayload 버퍼 크기 (GetPayloadSize 반환값과 같아야 함)
    template<typename TMsg>
    static bool Serialize(const TMsg& msg, uint8_t* pPayload, int32_t bufferSize)
    {
        if (bufferSize == 0)
            return true;  // 빈 메시지는 정상

        return msg.SerializeToArray(pPayload, bufferSize);
    }

    // payload 버퍼를 protobuf message로 역직렬화
    // @pPayload : PacketHeader 다음 위치의 포인터
    // @payloadSize : payload 크기
    template<typename TMsg>
    static bool Deserialize(const uint8_t* pPayload, int32_t payloadSize, TMsg& outMsg)
    {
        if (payloadSize <= 0)
            return true;  // 빈 메시지는 정상

        return outMsg.ParseFromArray(pPayload, payloadSize);
    }
};

} // namespace packet
