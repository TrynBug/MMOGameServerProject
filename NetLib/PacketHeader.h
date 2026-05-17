#pragma once

#include "Types.h"

namespace netlib
{

// 패킷 헤더
#pragma pack(push, 1)
struct PacketHeader
{
    uint16 size;      // 헤더 포함 전체 패킷 크기 (라이브러리가 사용)
    uint16 type;      // 메시지 ID (서버가 사용. 라이브러리는 사용안함)
    uint8  flags;     // 패킷 압축, 암호화 등의 플래그
    uint8  reserved;  // 메모리정렬, 추후 확장용 예약 필드
};
#pragma pack(pop)

static_assert(sizeof(PacketHeader) == 6, "PacketHeader must be 6 bytes");

// 패킷 플래그 비트
namespace PacketFlags
{
    constexpr uint8 None       = 0x00;
    constexpr uint8 Encrypted  = 0x01;  // 암호화됨
    constexpr uint8 Compressed = 0x02;  // 압축됨
    constexpr uint8 Sidecar    = 0x04;  // payload 앞에 Sidecar(부가 데이터)가 있음
}

// Sidecar 헤더
// PacketHeader 뒤에 Sidecar 헤더와 Sidecar 데이터를 넣을 수 있다. 이 때는 반드시 flags에 PacketFlags::Sidecar가 세팅되어야 한다.
// flags & PacketFlags::Sidecar 일 때 패킷 바이트구조: [PacketHeader][SidecarHeader][Sidecar 데이터(size 바이트)][실제 payload]
//
// 용도: 패킷 payload 데이터는 그대로 유지하면서 패킷에 추가적인 정보를 넣을 때 사용한다.
//       예를들면 게이트웨이서버에서 게임서버에 클라이언트 패킷을 중계할 때 사이드카를 사용하면 패킷에 UserId를 끼워넣을 수 있다.      
#pragma pack(push, 1)
struct SidecarHeader
{
    uint16 size;      // Sidecar 데이터 크기 (헤더 자체 4바이트는 미포함)
    uint16 reserved;  // 메모리정렬, 추후 확장
};
#pragma pack(pop)

static_assert(sizeof(SidecarHeader) == 4, "SidecarHeader must be 4 bytes");

} // namespace netlib
