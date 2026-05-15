#include "pch.h"

#include "PacketHeader.h"
#include "Packet.h"

#include <cassert>

namespace netlib
{

Packet::Packet(int32 capacity)
    : m_capacity(capacity)
{
    // capacity는 최소한 PacketHeader 크기보다 커야함
    assert(capacity >= sizeof(PacketHeader));

    m_buffer = new uint8[capacity];
    std::memset(m_buffer, 0, sizeof(PacketHeader));
}

Packet::~Packet()
{
    delete[] m_buffer;
    m_buffer = nullptr;
}

// header를 설정하고 payload 데이터를 write한다.
bool Packet::SetHeaderAndPayload(uint16 type, uint8 flags, const void* payload, int32 payloadSize)
{
    const int32 totalSize = static_cast<int32>(sizeof(PacketHeader)) + payloadSize;
    if (totalSize > m_capacity)
    {
        return false;
    }

    if (totalSize > 0xFFFF)
    {
        // uint16 초과
        return false;
    }

    // 헤더 설정
    PacketHeader* header = GetHeader();
    header->size     = static_cast<uint16>(totalSize);
    header->type     = type;
    header->flags    = flags;
    header->reserved = 0;

    // payload 복사
    if (payload != nullptr && payloadSize > 0)
    {
        std::memcpy(GetPayload(), payload, payloadSize);
    }

    m_offset = totalSize;

    return true;
}

// payload에 데이터를 추가로 write 한다. 헤더의 size를 갱신한다. (주의: 버퍼 크기 모자라면 write 실패함)
bool Packet::WritePayload(const char* pData, int32 size)
{
    const int32 totalSize = m_offset + size;
    if (totalSize > m_capacity)
    {
        return false;
    }

    if (totalSize > 0xFFFF)
    {
        // uint16 초과
        return false;
    }

    // payload 복사
    if (pData != nullptr && size > 0)
    {
        std::memcpy(m_buffer + m_offset, pData, size);
        m_offset += size;
    }

    // 헤더의 size 갱신
    GetHeader()->size = static_cast<uint16>(m_offset);

    return true;
}

// header 세팅
void Packet::SetHeader(uint16 size, uint16 type, uint8 flags)
{
    PacketHeader* header = GetHeader();
    header->size     = size;
    header->type     = type;
    header->flags    = flags;
    header->reserved = 0;
}

void Packet::Reset()
{
    std::memset(m_buffer, 0, sizeof(PacketHeader));
    m_offset = sizeof(PacketHeader);
}

} // namespace netlib
