#pragma once

#include "Types.h"
#include "NetConfig.h"
#include "INetBase.h"

#include <winsock2.h>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace netlib
{

class IoContext;
class Session;
class PacketPool;
class ISession;
class INetEventHandler;


// 서버가 다른 서버에 연결할때 쓰는 클라이언트
// 하나의 NetClient는 하나의 Session을 소유한다.
// ConnectEx(비동기)로 연결하고, 연결 실패 시 자동으로 재연결을 시도한다.
// 네트워크 인프라(IOCP, Worker스레드, PacketPool 등)는 IoContext가 담당한다. IoContext는 생성자로 전달받아서 보관한다. (IoContext를 분리한 이유는 NetServer와 NetClient에서 IoContext를 공통으로 사용하기 위해서이다)
// 네트워크 이벤트(Accept, Connect, Recv, Disconnect, Error)는 INetEventHandler 를 통해서 사용자에게 전달한다. 
// 사용 예:
//   IoContext ctx; ctx.Initialize(ioConfig);
//   NetClient gatewayLink(&ctx);
//   gatewayLink.Initialize(clientConfig);
//   gatewayLink.SetEventHandler(&handler);
//   gatewayLink.Connect("10.0.0.11", 8001);
//   ...
//   gatewayLink.Shutdown();
class NetClient : public INetBase
{
public:
    explicit NetClient(IoContext* ioContext);
    ~NetClient() override;

    NetClient(const NetClient&)            = delete;
    NetClient& operator=(const NetClient&) = delete;

public:
    bool Initialize(const NetClientConfig& config);
    void Shutdown();

    void SetEventHandler(INetEventHandler* handler) { m_eventHandler = handler; }

    // 재연결 시도 간격을 런타임에 변경한다.
    // 사용 예: ServerBase가 레지스트리 등록 성공 후 10초 → 1분으로 변경
    void SetReconnectIntervalMs(int32 intervalMs) { m_config.reconnectIntervalMs = intervalMs; }

    // 연결 시도. 실패하면 자동으로 재연결 한다. 기존 연결이 있으면 끊고 새로 연결한다.
    bool Connect(const std::string& ip, uint16 port);

    // 연결 끊기. 자동 재연결도 중단
    void Disconnect();

    std::shared_ptr<ISession> GetSession();

    bool IsConnected();

    INetEventHandler* GetEventHandler()              override { return m_eventHandler; }
    PacketPool&       GetPacketPool()                override;
    int32             GetMaxPacketSize() const       override;
    void              OnSessionDisconnected(std::shared_ptr<ISession> spSession) override;

private:
    // 실제 ConnectEx 시작. 새 Session 객체를 만들어 m_session에 세팅.
    bool postConnect();

    // 재연결 스레드
    void reconnectThreadProc();

    // 소켓 옵션 (Nagle/Linger).
    void setSocketOptions(SOCKET s);

    // ConnectEx 함수 포인터를 WSAIoctl로 획득
    LPFN_CONNECTEX loadConnectEx(SOCKET s);

private:
    IoContext*                   m_ioContext    = nullptr;
    NetClientConfig              m_config;

    INetEventHandler*            m_eventHandler = nullptr;

    // Session
    std::mutex                   m_sessionMutex;
    std::shared_ptr<Session>     m_session;

    // 연결할 서버
    std::string                  m_remoteIp;
    uint16                       m_remotePort   = 0;

    // 재연결 제어
    std::thread                  m_reconnectThread;
    std::atomic<bool>            m_bReconnectRunning { false };
    std::atomic<bool>            m_bShouldConnect    { false }; // 연결을 시도해야하는지 여부. Connect 호출 후 true. Disconnect로 false.
    std::mutex                   m_reconnectMutex;
    std::condition_variable      m_reconnectCv;     // 재연결 스레드를 깨우기위한 cv

    std::atomic<int32>           m_reconnectAttempts { 0 };   // 재연결 시도횟수

    // Session ID 생성용 변수
    std::atomic<int64>           m_nextSessionId { 1 };
};

using NetClientPtr = std::shared_ptr<NetClient>;
using NetClientWPtr = std::weak_ptr<NetClient>;
using NetClientUPtr = std::unique_ptr<NetClient>;

} // namespace netlib
