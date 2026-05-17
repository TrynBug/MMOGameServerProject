#pragma once

#include "Types.h"
#include "PacketHeader.h"

#include <memory>

namespace netlib
{

// 패킷 버퍼 클래스. PacketPool을 통해 할당받는다.
// 버퍼 메모리에는 데이터가 [PacketHeader][Payload] 형태로 입력됨
//
// 사이드카(Sidecar) 기능:
//   PacketFlags::Sidecar 비트가 켜져 있으면 메모리 레이아웃은 다음과 같다:
//     [PacketHeader][SidecarHeader][사이드카 데이터][실제 payload]
//   GetPayload()/GetPayloadSize()는 자동으로 사이드카 이후의 실제 payload를 반환한다.
//   사이드카는 라우팅 정보(userId 등) 부가 데이터를 새 패킷 할당 없이 끼워넣는 용도.
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

    // Payload (사이드카가 있으면 사이드카 이후를 가리킴)
    uint8*       GetPayload();
    const uint8* GetPayload() const;
    int32        GetPayloadSize() const;

    // buffer (header 포함)
    uint8*       GetRawBuffer()       { return m_buffer; }
    const uint8* GetRawBuffer() const { return m_buffer; }
    int32        GetTotalSize() const { return GetHeader()->size; }
    int32        GetCapacity()  const { return m_capacity; }

    // payload에 데이터를 추가로 write 한다. 헤더의 size를 갱신한다. (주의: 버퍼 크기 모자라면 write 실패함)
    bool WritePayload(const char* pData, int32 size);

    // header 세팅
    void SetHeader(uint16 size, uint16 type, uint8 flags);

    // ── Sidecar API ─────────────────────────────────────────────
    // 사이드카가 있는지 (flags의 Sidecar 비트 확인)
    bool HasSidecar() const;

    // 사이드카 데이터 포인터/크기. 없으면 nullptr/0 반환.
    const uint8* GetSidecarData() const;
    int32        GetSidecarSize() const;

    // 사이드카 설정.
    // - 기존 payload를 sizeof(SidecarHeader) + size 바이트 뒤로 시프트한다(memmove).
    // - 그 자리에 SidecarHeader와 사이드카 데이터를 쓴다.
    // - flags의 Sidecar 비트를 set하고, header.size를 갱신한다.
    // - capacity 부족 또는 이미 사이드카가 있으면 false 반환.
    // 주의: 호출 후 GetPayload()/GetPayloadSize()는 자동으로 사이드카 이후를 가리킨다.
    bool SetSidecar(const void* data, int32 size);

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
