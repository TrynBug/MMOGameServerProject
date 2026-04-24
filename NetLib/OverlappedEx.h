#pragma once

#include "Types.h"
#include <winsock2.h>
#include <memory>
#include <cstring>

namespace netlib
{

class ISession;


enum class IO_TYPE : uint8
{
    None    = 0,
    Recv    = 1,
    Send    = 2,
    Accept  = 3,
    Connect = 4,     // ConnectEx 완료통지 (NetClient에서 사용)
};


// OVERLAPPED 구조체 확장
struct OVERLAPPED_EX
{
    OVERLAPPED                 overlapped;
    IO_TYPE                    ioType = IO_TYPE::None;
    std::shared_ptr<ISession>  spSession;                // IO 도중 세션이 소멸되는것 방지

    OVERLAPPED_EX()
    {
        std::memset(&overlapped, 0, sizeof(overlapped));
    }

    void Reset()
    {
        std::memset(&overlapped, 0, sizeof(overlapped));
        // ioType, spSession은 라이브러리가 세팅/해제
    }
};

} // namespace netlib
