#pragma once

#include "Types.h"

#include <chrono>
#include <functional>
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

    // deliverAt(절대 시각)에 fn 을 실행하도록 예약한다(IoContext 의 지연 스케줄러로 forward). 지연 시뮬레이션용.
    virtual void ScheduleAt(std::chrono::steady_clock::time_point deliverAt, std::function<void()> fn) = 0;
};

}
