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
}

} // namespace netlib
