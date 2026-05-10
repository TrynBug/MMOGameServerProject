#pragma once

#include "INetEventHandler.h"
#include "ISession.h"
#include "Packet.h"

#include <functional>
#include <string>

namespace netlib
{

// INetEventHandler의 함수 기반 구현체. 각 콜백을 std::function으로 등록한다.
// 사용 예:
//   FuncEventHandler m_gameEventHandler;  // 클래스 멤버변수로 선언
// 
//   // 초기화 함수 등에서 콜백 등록
//   m_gameEventHandler.onConnect    = [this](auto sp)           { onGameConnect(sp); };
//   m_gameEventHandler.onRecv       = [this](auto sp, auto pkt) { onGameRecv(sp, pkt); };
//   m_gameEventHandler.onDisconnect = [this](auto sp)           { onGameDisconnect(sp); };
//   netClient.SetEventHandler(&m_gameEventHandler);
class FuncEventHandler : public INetEventHandler
{
public:
    std::function<bool(ISessionPtr)>                     onAccept;
    std::function<void(ISessionPtr)>                     onConnect;
    std::function<void(ISessionPtr, PacketPtr)>          onRecv;
    std::function<void(ISessionPtr)>                     onDisconnect;
    std::function<void(LogLevel, ISessionPtr, const std::string&)> onLog;

    bool OnAccept(ISessionPtr spSession) override
    {
        return onAccept ? onAccept(std::move(spSession)) : true;
    }

    void OnConnect(ISessionPtr spSession) override
    {
        if (onConnect)
            onConnect(std::move(spSession));
    }

    void OnRecv(ISessionPtr spSession, PacketPtr spPacket) override
    {
        if (onRecv)
            onRecv(std::move(spSession), std::move(spPacket));
    }

    void OnDisconnect(ISessionPtr spSession) override
    {
        if (onDisconnect)
            onDisconnect(std::move(spSession));
    }

    void OnLog(LogLevel logLevel, ISessionPtr spSession, const std::string& msg) override
    {
        if (onLog)
            onLog(logLevel, std::move(spSession), msg);
    }
};

} // namespace netlib
