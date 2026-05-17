#pragma once

#include "Types.h"

#include <memory>

namespace netlib
{

class INetEventHandler;
class PacketPool;
class ISession;

// 네트워크 클래스의 Base클래스이다.
// NetServer, NetClient가 INetBase를 상속받는다.
class INetBase
{
public:
    virtual ~INetBase() = default;

    virtual INetEventHandler* GetEventHandler() = 0;
    virtual PacketPool&       GetPacketPool()  = 0;
    virtual int32             GetMaxPacketSize() const = 0;

    // Session 연결이 끊겼을 때 호출되는 콜백함수
    virtual void OnSessionDisconnected(std::shared_ptr<ISession> spSession) = 0;
};

}
