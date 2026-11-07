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
//   m_gameEventHandler.onConnect      = [this](auto sp)           { onGameConnect(sp); };
//   m_gameEventHandler.onRecv         = [this](auto sp, auto pkt) { onGameRecv(sp, pkt); };
//   m_gameEventHandler.onSendComplete = [this](auto sp)           { onGameSendComplete(sp); };
//   m_gameEventHandler.onDisconnect   = [this](auto sp)           { onGameDisconnect(sp); };
//   netClient.SetEventHandler(&m_gameEventHandler);
class FuncEventHandler : public INetEventHandler
{
public:
    std::function<bool(const ISessionPtr&)>                          onAccept;
    std::function<void(const ISessionPtr&)>                          onConnect;
    std::function<void(const ISessionPtr&, const PacketPtr&)>        onRecv;
    std::function<void(const ISessionPtr&)>                          onSendComplete;
    std::function<void(const ISessionPtr&)>                          onDisconnect;
    std::function<void(LogLevel, const ISessionPtr&, const std::string&)> onLog;

    bool OnAccept(const ISessionPtr& spSession) override
    {
        return onAccept ? onAccept(spSession) : true;
    }

    void OnConnect(const ISessionPtr& spSession) override
    {
        if (onConnect)
            onConnect(spSession);
    }

    void OnRecv(const ISessionPtr& spSession, const PacketPtr& spPacket) override
    {
        if (onRecv)
            onRecv(spSession, spPacket);
    }

    void OnSendComplete(const ISessionPtr& spSession) override
    {
        if (onSendComplete)
            onSendComplete(spSession);
    }

    void OnDisconnect(const ISessionPtr& spSession) override
    {
        if (onDisconnect)
            onDisconnect(spSession);
    }

    void OnLog(LogLevel logLevel, const ISessionPtr& spSession, const std::string& msg) override
    {
        if (onLog)
            onLog(logLevel, spSession, msg);
    }
};

} // namespace netlib
