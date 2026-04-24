#pragma once

#include "Types.h"
#include "ISession.h"
#include "RingBuffer.h"
#include "OverlappedEx.h"
#include "Packet.h"

#include <winsock2.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

namespace netlib
{

class INetBase;

// IOCP 기반 TCP 세션. ISession 인터페이스를 구현한다.
class Session : public ISession, public std::enable_shared_from_this<Session>
{
public:
    static constexpr int32 SEND_WSABUF_MAX_SIZE = 50;   // send할 때 WSABUF 배열 최대크기

    Session(INetBase* pNetBase, int64 sessionId, SOCKET socket, const std::string& ip, uint16 port, int32 recvBufSize);
    ~Session() override;

    /* ISession */
    void        Send(PacketPtr& spPacket) override;
    void        Disconnect() override;
    int64       GetId()       const override { return m_sessionId; }
    std::string GetIP()       const override { return m_ip; }
    uint16      GetPort()     const override { return m_port; }
    bool        IsConnected() const override { return m_bConnected.load(); }

    /* Session */
    SOCKET      GetSocket() const { return m_socket; }
    OVERLAPPED_EX& GetConnectOverlapped() { return m_connectOverlapped; }

    void SetIp(const std::string& ip)   { m_ip = ip; }
    void SetPort(uint16 port)           { m_port = port; }
    void SetConnected(bool connected)   { m_bConnected.store(connected); }

    /* Network */
    void        StartRecv();

    // worker 스레드가 recv, send 완료통지 받았을때 호출해주는 콜백함수
    void        OnRecvCompleted(DWORD bytesTransferred);  
    void        OnSendCompleted(DWORD bytesTransferred);

    // ConnectEx 완료통지 콜백함수 (NetClient에서만 사용)
    void        OnConnectCompleted(bool success);

    void        CloseSocket();

private:
    /* Network */
    void        parseReceivedPackets();
    bool        postRecv();
    bool        postSend();
    void        trySendNext();

private:
    INetBase*                     m_pNetBase   = nullptr;     // 세션이 속한 Network 객체
    int64                         m_sessionId  = 0;
    SOCKET                        m_socket     = INVALID_SOCKET;
    std::string                   m_ip;
    uint16                        m_port       = 0;

    RingBuffer                    m_recvBuf;                // 수신버퍼
    OVERLAPPED_EX                 m_recvOverlapped;         // recv 전용 overlapped
    std::atomic<bool>             m_bRecving   { false };   // recv 중인지 여부

    std::mutex                    m_sendMutex;              // send 전용 mutex
    std::queue<PacketPtr>         m_sendQueue;              // send 대기중인 패킷 모아두는 큐
    OVERLAPPED_EX                 m_sendOverlapped;         // send 전용 overlapped
    std::atomic<bool>             m_bSending   { false };   // send 중인지 여부
    std::vector<PacketPtr>        m_sendingPackets;         // send 하는중인 패킷 모아두는 벡터 (Packet 객체가 send중에 소멸되는것 방지)

    OVERLAPPED_EX                 m_connectOverlapped;      // ConnectEx 전용 overlapped (NetClient에서만 사용)

    std::atomic<bool>             m_bConnected { false };   // 연결됨 여부
    std::atomic<bool>             m_bClosed    { false };   // 소켓닫힘 여부
};

using SessionPtr = std::shared_ptr<Session>;
using SessionWPtr = std::weak_ptr<Session>;

} // namespace netlib
