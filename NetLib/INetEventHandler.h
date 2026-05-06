#pragma once

#include "ISession.h"
#include "Packet.h"

#include <string>

namespace netlib
{

// 네트워크 이벤트 처리 인터페이스
// 일반적으로는 INetEventHandler를 구현한 FuncEventHandler를 사용하면 된다.
class INetEventHandler
{
public:
    virtual ~INetEventHandler() = default;

    // Accept 되었을 때 호출. 사용자가 true를 리턴하면 Session 객체 생성, false면 연결 끊음. (NetClient에서는 호출되지 않음)
    virtual bool OnAccept(ISessionPtr spSession) = 0;

    // Session 객체가 생성되고 연결이 완료되었을 때 호출
    virtual void OnConnect(ISessionPtr spSession) = 0;

    // 패킷 1개를 수신했을 때 호출
    virtual void OnRecv(ISessionPtr spSession, PacketPtr spPacket) = 0;

    // Session 연결이 끊겼을 때 호출
    virtual void OnDisconnect(ISessionPtr spSession) = 0;

    // 오류가 발생했을 때 호출
    virtual void OnError(ISessionPtr spSession, const std::string& msg) = 0;
};

} // namespace netlib
