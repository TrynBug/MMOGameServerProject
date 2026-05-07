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

    virtual void        Send(PacketPtr& spPacket)    = 0;
    virtual void        Disconnect()                = 0;
    virtual int64       GetId()       const         = 0;
    virtual std::string GetIP()       const         = 0;
    virtual uint16      GetPort()     const         = 0;
    virtual bool        IsConnected() const         = 0;
};

using ISessionPtr  = std::shared_ptr<ISession>;
using ISessionWPtr = std::weak_ptr<ISession>;
using ISessionUPtr = std::unique_ptr<ISession>;

} // namespace netlib
