#pragma once

#include "Types.h"
#include "Packet.h"

#include <memory>
#include <string>

namespace netlib
{

// 세션 인터페이스
// 라이브러리 내부에서는 Session 클래스가 ISession을 상속받아서 세션을 구현한다.
// 라이브러리 외부에는 ISession만 노출된다. (Session은 노출되지 않음, 보안 목적으로 이렇게 함)
class ISession
{
public:
    virtual ~ISession() = default;

    virtual void        Send(const PacketPtr& spPacket) = 0;
    virtual void        Disconnect()                = 0;
    virtual int64       GetId()       const         = 0;
    virtual std::string GetIP()       const         = 0;
    virtual uint16      GetPort()     const         = 0;
    virtual bool        IsConnected() const         = 0;

    // 사용자 정의 데이터 슬롯. 라이브러리는 이 값을 읽거나 해석하지 않는다.
    virtual void                  SetUserData(std::shared_ptr<void> spData) = 0;
    virtual std::shared_ptr<void> GetUserData() const                       = 0;

    // 이 세션의 수신/송신을 일부러 지연시킨다(네트워크 지연 시뮬레이션). 0 = 지연 없음.
    // 수신: OnRecv 호출을 recvMs 만큼 지연. 송신: Send 를 sendMs 만큼 지연. 런타임 변경 가능.
    virtual void SetSimulatedDelay(int32 recvMs, int32 sendMs) = 0;
};

using ISessionPtr  = std::shared_ptr<ISession>;
using ISessionWPtr = std::weak_ptr<ISession>;
using ISessionUPtr = std::unique_ptr<ISession>;

} // namespace netlib
