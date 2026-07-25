#pragma once

#include "Types.h"
#include "ISession.h"
#include "RingBuffer.h"
#include "OverlappedEx.h"
#include "Packet.h"

#include <winsock2.h>
#include <atomic>
#include <chrono>
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
    void        Send(const PacketPtr& spPacket) override;  // thread-safe 한 Send 함수
    void        Disconnect() override;
    int64       GetId()       const override { return m_sessionId; }
    std::string GetIP()       const override { return m_ip; }
    uint16      GetPort()     const override { return m_port; }
    bool        IsConnected() const override { return m_bConnected.load(); }

    // 사용자 정의 데이터 슬롯. 라이브러리는 이 값을 읽거나 해석하지 않는다.
    void                  SetUserData(std::shared_ptr<void> spData) override { m_spUserData = std::move(spData); }
    std::shared_ptr<void> GetUserData() const override                       { return m_spUserData; }

    /* Session */
    SOCKET      GetSocket() const { return m_socket; }
    OVERLAPPED_EX& GetConnectOverlapped() { return m_connectOverlapped; }

    void SetIp(const std::string& ip)   { m_ip = ip; }
    void SetPort(uint16 port)           { m_port = port; }
    void SetConnected(bool connected);

    /* Network */
    void        StartRecv();

    // worker 스레드가 recv, send 완료통지 받았을때 호출해주는 콜백함수
    void        OnRecvCompleted(DWORD bytesTransferred);  
    void        OnSendCompleted(DWORD bytesTransferred);

    // ConnectEx 완료통지 콜백함수 (NetClient에서만 사용)
    void        OnConnectCompleted(bool success);

    void        CloseSocket();

    /* 네트워크 지연 시뮬레이션 */
    // 네트워크 지연 시뮬레이션 설정. 런타임 변경 가능. (테스트 전용)
    void SetSimulatedDelay(int32 recvMs, int32 sendMs) override;

private:
    /* Network */
    void        parseReceivedPackets();
    bool        postRecv();
    bool        postSend();
    void        trySendNext();

    // sendQueue 에 패킷을 넣고, send 진행중이 아니면 송신을 시작한다. (Send 의 실제 송신 부분)
    void        enqueueAndKickSend(const PacketPtr& spPacket);
    void        clearSendingPackets();
    void        clearSendQueue();

    /* 네트워크 지연 시뮬레이션 */
    // 송신/수신을 순서 보존하며 지연 후 처리하도록 스케줄한다. (네트워크 지연 파이프 활성 상태에서만 호출)
    void        scheduleDelayedSend(const PacketPtr& spPacket);
    void        scheduleDelayedRecv(const PacketPtr& spPacket);
    // 스케줄러 스레드가 deliver 시각에 호출. 실제 송신/수신 후 pending 감소 + 네트워크 지연 파이프 비면 비활성화.
    void        releaseDelayedSend(const PacketPtr& spPacket);
    void        releaseDelayedRecv(const PacketPtr& spPacket);

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

    std::shared_ptr<void>         m_spUserData;             // 사용자 정의 데이터 슬롯

    /* 네트워크 지연 시뮬레이션 */
    // 네트워크 지연 시뮬레이션 (ms, 0 = 지연 없음). m_delayStateMutex 보호 하에서만 접근.
    std::atomic<int32>            m_recvDelayMs{ 0 };
    std::atomic<int32>            m_sendDelayMs{ 0 };

    // 지연 파이프 활성 여부. Send/recv 핫패스는 이 atomic 1개만 보고 fast/slow 를 가른다(평상시 false → lock 없음).
    // true = 지연이 켜져 있거나 아직 방출 안 된 지연 패킷이 남아있음.
    std::atomic<bool>             m_sendDelayActive{ false };
    std::atomic<bool>             m_recvDelayActive{ false };

    // 지연 시뮬레이션 순서 보존 상태 (m_delayStateMutex 보호).
    // cursor: 마지막으로 스케줄된 deliver 시각(단조). pending: 아직 방출되지 않은 지연 패킷 수.
    // pending>0 이면 지연이 0 으로 바뀌어도 fast path 를 타지 않고 파이프에 줄세워 순서를 보존한다.
    std::mutex                            m_delayStateMutex;
    std::chrono::steady_clock::time_point m_recvDeliverCursor{};
    std::chrono::steady_clock::time_point m_sendDeliverCursor{};
    int32                                 m_pendingDelayedRecvs = 0;
    int32                                 m_pendingDelayedSends = 0;
};

using SessionPtr = std::shared_ptr<Session>;
using SessionWPtr = std::weak_ptr<Session>;
using SessionUPtr = std::unique_ptr<Session>;

} // namespace netlib
