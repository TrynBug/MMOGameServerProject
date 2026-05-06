#pragma once

#include "Types.h"
#include "NetConfig.h"
#include "INetBase.h"

#include <winsock2.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace netlib
{

class IoContext;
class Session;
class PacketPool;
class ISession;
class INetEventHandler;

// 네트워크 서버
// listen 소켓, Accept 스레드, Session map을 관리한다.
// 네트워크 인프라(IOCP, Worker스레드, PacketPool 등)는 IoContext가 담당한다. IoContext는 생성자로 전달받아서 보관한다. (IoContext를 분리한 이유는 NetServer와 NetClient에서 IoContext를 공통으로 사용하기 위해서이다)
// 네트워크 이벤트(Accept, Connect, Recv, Disconnect, Error)는 INetEventHandler 를 통해서 사용자에게 전달한다. 
class NetServer : public INetBase
{
public:
    explicit NetServer(IoContext* pIoContext);
    ~NetServer() override;

    NetServer(const NetServer&)            = delete;
    NetServer& operator=(const NetServer&) = delete;

public:
    /* 서버 */
    bool Initialize(const NetServerConfig& config);
    bool StartAccept();
    void StopAccept();
    void Shutdown();

    /* 핸들러 등록 */
    void              SetEventHandler(INetEventHandler* handler) { m_eventHandler = handler; }

    /* 세션 관리 */
    std::shared_ptr<ISession> FindSession(int64 sessionId);
    void                     RemoveSession(int64 sessionId);
    int32                    GetSessionCount();

    /* INetBase */
    INetEventHandler* GetEventHandler()              override { return m_eventHandler; }
    PacketPool&       GetPacketPool()                override;
    int32             GetMaxPacketSize() const       override;
    void              OnSessionDisconnected(std::shared_ptr<ISession> spSession) override;

private:
    void  acceptThreadProc();
    bool  registerNewSession(SOCKET clientSocket, const std::string& ip, uint16 port);
    int64 generateSessionId();
    void  setSocketOptions(SOCKET s);

private:
    IoContext*                                                 m_pIoContext    = nullptr;
    NetServerConfig                                            m_config;

    SOCKET                                                     m_listenSocket = INVALID_SOCKET;
    std::thread                                                m_acceptThread;

    std::atomic<bool>                                          m_bAccepting   { false };

    std::mutex                                                 m_sessionMutex;
    std::unordered_map<int64, std::shared_ptr<Session>>        m_sessions;
    std::atomic<int64>                                         m_nextSessionId { 1 };

    INetEventHandler*                                          m_eventHandler = nullptr;
};

} // namespace netlib
