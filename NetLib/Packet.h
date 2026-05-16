#pragma once

#include "Types.h"
#include "PacketHeader.h"

#include <memory>

namespace netlib
{

// 패킷 버퍼 클래스. PacketPool을 통해 할당받는다.
// 버퍼 메모리에는 데이터가 [PacketHeader][Payload] 형태로 입력됨
class Packet
{
public:
    Packet(int32 capacity);
    ~Packet();

    Packet(const Packet&)            = delete;
    Packet& operator=(const Packet&) = delete;

    // Header
    PacketHeader*       GetHeader()       { return reinterpret_cast<PacketHeader*>(m_buffer); }
    const PacketHeader* GetHeader() const { return reinterpret_cast<const PacketHeader*>(m_buffer); }

    // Payload
    uint8*       GetPayload()       { return m_buffer + sizeof(PacketHeader); }
    const uint8* GetPayload() const { return m_buffer + sizeof(PacketHeader); }
    int32        GetPayloadSize() const { return static_cast<int32>(GetHeader()->size) - static_cast<int32>(sizeof(PacketHeader)); }

    // buffer (header 포함)
    uint8*       GetRawBuffer()       { return m_buffer; }
    const uint8* GetRawBuffer() const { return m_buffer; }
    int32        GetTotalSize() const { return GetHeader()->size; }
    int32        GetCapacity()  const { return m_capacity; }

    // header를 설정하고 payload 데이터를 write한다.
    bool SetHeaderAndPayload(uint16 type, uint8 flags, const void* payload, int32 payloadSize);

    // payload에 데이터를 추가로 write 한다. 헤더의 size를 갱신한다. (주의: 버퍼 크기 모자라면 write 실패함)
    bool WritePayload(const char* pData, int32 size);

    // header 세팅
    void SetHeader(uint16 size, uint16 type, uint8 flags);

    // 초기화
    void Reset();

private:
    uint8*  m_buffer   = nullptr;
    int32   m_capacity = 0;   // 버퍼 전체 크기
    int32   m_offset = sizeof(PacketHeader);   // 현재 사용한 버퍼 끝 위치(헤더위치는 기본적으로 사용한것으로 취급함)
};

using PacketPtr  = std::shared_ptr<Packet>;
using PacketWPtr = std::weak_ptr<Packet>;
using PacketUPtr = std::unique_ptr<Packet>;

} // namespace netlib
