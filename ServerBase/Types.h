#pragma once

// 변수 타입 정의
// NetLib/Types.h 와 내용이 동일하므로 중복 정의 방지를 위해 guard로 보호
#ifndef NETLIB_TYPES_DEFINED
#define NETLIB_TYPES_DEFINED
using int8   = signed char;
using int16  = signed short;
using int32  = signed int;
using int64  = signed __int64;
using uint8  = unsigned char;
using uint16 = unsigned short;
using uint32 = unsigned int;
using uint64 = unsigned __int64;
#endif

#include <string>

// 서버 타입
enum class ServerType : uint8
{
    Unknown     = 0,
    Registry    = 1,
    Login       = 2,
    Gateway     = 3,
    Game        = 4,
    Chat        = 5,
};

// 서버 상태
enum class ServerStatus : uint8
{
    Unknown        = 0,
    Running        = 1,   // 정상 가동 중
    ShuttingDown   = 2,   // Graceful Shutdown 대기 중 (신규 유저 접속 불가)
    Disconnected   = 3,   // 연결 끊김
};

// 레지스트리 서버에 등록된 다른 서버의 정보
struct ServerInfo
{
    int32        serverId   = 0;
    ServerType   serverType = ServerType::Unknown;
    ServerStatus status     = ServerStatus::Unknown;
    std::string  ip;
    uint16       port       = 0;
    int32        userCount  = 0;   // 접속자 수 (게이트웨이, 게임서버만 유효)
};
