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

uint8* Packet::GetPayload()
{
    if (HasSidecar())
    {
        // Sidecar가 있으면 payload의 시작은 PacketHeader + SidecarHeader + Sidecar 크기 이후임
        return m_buffer + sizeof(PacketHeader) + sizeof(SidecarHeader) + GetSidecarSize();
    }

    return m_buffer + sizeof(PacketHeader);
}

const uint8* Packet::GetPayload() const
{
    if (HasSidecar())
    {
        return m_buffer + sizeof(PacketHeader) + sizeof(SidecarHeader) + GetSidecarSize();
    }

    return m_buffer + sizeof(PacketHeader);
}

int32 Packet::GetPayloadSize() const
{
    const int32 totalSize = static_cast<int32>(GetHeader()->size);
    int32 prefixSize = static_cast<int32>(sizeof(PacketHeader));
    if (HasSidecar())
    {
        prefixSize += static_cast<int32>(sizeof(SidecarHeader)) + GetSidecarSize();
    }

    return totalSize - prefixSize;
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

// Sidecar 있는지 여부
bool Packet::HasSidecar() const
{
    return (GetHeader()->flags & PacketFlags::Sidecar) != 0;
}

const uint8* Packet::GetSidecarData() const
{
    if (!HasSidecar())
        return nullptr;

    return m_buffer + sizeof(PacketHeader) + sizeof(SidecarHeader);
}

int32 Packet::GetSidecarSize() const
{
    if (!HasSidecar())
        return 0;

    const SidecarHeader* pSidecar = reinterpret_cast<const SidecarHeader*>(m_buffer + sizeof(PacketHeader));
    return static_cast<int32>(pSidecar->size);
}

bool Packet::SetSidecar(const void* data, int32 size)
{
    // 잘못된 입력 체크
    if (size < 0 || size > 0xFFFF)
        return false;

    // 이미 Sidecar가 있으면 거부 (중복 추가 방지. 필요하면 향후 ReplaceSidecar 추가)
    if (HasSidecar())
        return false;

    // 현재 버퍼구조: [PacketHeader][payload(payloadSize)]
    // 목표 버퍼구조: [PacketHeader][SidecarHeader][Sidecar 데이터(size)][payload]
    const int32 currentTotalSize = static_cast<int32>(GetHeader()->size);
    const int32 currentPayloadSize = currentTotalSize - static_cast<int32>(sizeof(PacketHeader));
    const int32 sidecarBlockSize = static_cast<int32>(sizeof(SidecarHeader)) + size;
    const int32 newTotalSize = currentTotalSize + sidecarBlockSize;

    if (newTotalSize > m_capacity)
        return false;

    if (newTotalSize > 0xFFFF)
        return false;

    // payload를 뒤로 shift. SidecarHeader + Sidecar 데이터 크기만큼 뒤로 밀어야함
    uint8* payloadCurrent = m_buffer + sizeof(PacketHeader);
    uint8* payloadNew = payloadCurrent + sidecarBlockSize;
    if (currentPayloadSize > 0)
    {
        // memmove 사용하여 빠르게 shift
        std::memmove(payloadNew, payloadCurrent, currentPayloadSize);
    }

    // Sidecar 헤더 입력
    SidecarHeader* pSidecar = reinterpret_cast<SidecarHeader*>(m_buffer + sizeof(PacketHeader));
    pSidecar->size = static_cast<uint16>(size);
    pSidecar->reserved = 0;

    // Sidecar 데이터 입력
    if (data != nullptr && size > 0)
    {
        std::memcpy(m_buffer + sizeof(PacketHeader) + sizeof(SidecarHeader), data, size);
    }

    // 헤더 flags 및 size 갱신
    PacketHeader* header = GetHeader();
    header->flags |= PacketFlags::Sidecar;
    header->size = static_cast<uint16>(newTotalSize);

    m_offset = newTotalSize;

    return true;
}

void Packet::Reset()
{
    std::memset(m_buffer, 0, sizeof(PacketHeader));
    m_offset = sizeof(PacketHeader);
}

} // namespace netlib
