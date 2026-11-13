#pragma once

#include "../NetLibCommon/NetDefine.h"
#include "../NetLibCommon/INetEventHandler.h"
#include "../NetLibCommon/RingBuffer.h"

#include <boost/asio.hpp>
#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>

namespace netlib
{

namespace asio = boost::asio;
using Tcp = asio::ip::tcp;
using ErrorCode = boost::system::error_code;
using EventHandlerSlot = std::atomic<INetEventHandler*>; // 등록한 객체는 호출자가 관리하고, 세션은 주소를 읽는다.

class PacketPool;

// TCP 연결 하나의 소켓, 패킷 조립, 송신 순서와 종료를 관리한다.
class Session : public ISession, public std::enable_shared_from_this<Session>
{
public:
    Session(asio::io_context& context, PacketPool& packetPool, int32 maxPacketSize,
            int64 sessionId, Tcp::socket socket, const Tcp::endpoint& remote,
            int32 recvBufSize, int32 recvPacketReserveSize,
            std::shared_ptr<EventHandlerSlot> spHandler, std::function<void(int64)> onDisconnect);
    ~Session() override;

    void Send(const PacketPtr& spPacket) override;
    void Disconnect() override;
    void SendAndDisconnect(const PacketPtr& spPacket) override;
    int64 GetId() const override { return m_sessionId; }
    std::string GetIP() const override { return m_ip; }
    uint16 GetPort() const override { return m_port; }
    bool IsConnected() const override { return m_bConnected.load() && !m_bDisconnectRequested.load(); }

    void SetUserData(std::shared_ptr<void> spData) override { m_spUserData.store(std::move(spData)); }
    std::shared_ptr<void> GetUserData() const override { return m_spUserData.load(); }
    // 패킷 지연 테스트용. 밀리초 단위이며 0은 새 지연을 추가하지 않는 설정이다.
    void SetSimulatedDelay(int32 recvMs, int32 sendMs) override;

    void Start(bool bAccept);
    void Close();
    static void SetSocketOptions(Tcp::socket& socket, bool bUseNagle);

private:
    // 패킷 지연 테스트에서 사용할 전달 예정 시각과 패킷.
    using DelayedPacket = std::pair<std::chrono::steady_clock::time_point, PacketPtr>;

    struct DelayQueue
    {
        explicit DelayQueue(asio::io_context& context) : timer(context) {}

        asio::steady_timer timer;          // 큐의 맨 앞 패킷이 전달될 시각을 기다린다.
        std::deque<DelayedPacket> packets; // 테스트 지연이 끝날 때까지 도착 순서대로 보관한다.
    };

    void postRecv();
    void parseReceivedPackets();
    void queueSend(const PacketPtr& spPacket, bool bDisconnectAfterSend);
    void enqueueSend(const PacketPtr& spPacket);
    void startSend();
    void postSend();
    void clearSendingPackets();
    void queueDelayedPacket(DelayQueue& queue, DelayedPacket packet, bool bSend);
    void armDelayTimer(DelayQueue& queue, bool bSend);
    void deliverPacket(const PacketPtr& spPacket);
    void closeOnError(const ErrorCode& error, bool bSend);
    void logError(const std::string& message);

    // 소켓, 타이머, 콜백은 세션별로 직렬 처리한다. 수신 오류 처리처럼 같은 스레드에서 Close()로
    // 이어지는 경로가 있어 recursive_mutex를 사용한다.
    // 두 락이 모두 필요한 경로는 세션 락을 먼저 잡는다. 외부 Send()/Disconnect()는 게시용 락만 잡는다.
    std::recursive_mutex m_sessionMutex; // 세션 상태와 I/O 완료 콜백 직렬화
    std::mutex m_postMutex;              // 외부 송신·종료 요청과 워커의 대기열 인수를 보호

    asio::io_context* m_pContext;       // 이 세션의 I/O와 게시 작업을 처리하는 공용 컨텍스트
    PacketPool& m_packetPool;           // 수신한 패킷을 담을 버퍼를 할당하는 공용 풀
    const int32 m_maxPacketSize;        // 여유 공간을 포함해 풀에서 할당 가능한 최대 패킷 크기
    const int64 m_sessionId;            // NetServer 또는 NetClient가 부여한 연결 ID
    const std::string m_ip;             // 상대방 IP 주소
    const uint16 m_port;                // 상대방 TCP 포트
    const int32 m_recvPacketReserveSize; // Sidecar 추가 등을 위해 수신 버퍼에 확보할 여유 크기
    std::unique_ptr<Tcp::socket> m_socket; // 연결의 실제 TCP 소켓. Close()에서 해제한다.
    std::unique_ptr<DelayQueue> m_delayedRecvs; // 패킷 지연 테스트용 수신 큐와 타이머
    std::unique_ptr<DelayQueue> m_delayedSends; // 패킷 지연 테스트용 송신 큐와 타이머
    std::shared_ptr<EventHandlerSlot> m_spHandler; // 상위 코드에 이벤트를 전달하는 공유 슬롯
    std::function<void(int64)> m_onDisconnect;    // 소유자의 세션 목록을 정리하는 내부 콜백
    std::atomic<bool> m_bConnected{ false };      // Start()에서 설정하고 Close()에서 해제하는 연결 상태
    std::atomic<bool> m_bDisconnectRequested{ false }; // 소켓을 닫기 전에도 추가 송신을 차단하는 종료 요청
    bool m_bDisconnectAfterSend = false; // m_postMutex 보호. 마지막 송신 요청을 받은 뒤 추가 요청을 차단한다.
    std::unique_ptr<asio::steady_timer> m_disconnectTimer; // m_sessionMutex 보호. 워커가 마지막 송신을 인수하면 생성한다.
    std::atomic<std::shared_ptr<void>> m_spUserData;   // 상위 코드가 세션과 함께 보관하는 데이터

    RingBuffer m_recvBuf;                   // 소켓에서 읽은 바이트와 아직 덜 도착한 패킷을 보관
    std::deque<DelayedPacket> m_pendingSends; // 호출 스레드가 워커에 넘길 패킷과 전달 예정 시각
    std::deque<PacketPtr> m_sendQueue;       // 워커가 소켓에 쓰기 위해 준비한 패킷
    // 현재 배치의 PacketPtr를 보관해 비동기 쓰기가 참조하는 메모리 수명을 유지한다.
    std::vector<PacketPtr> m_sendingPackets;
    // async_write_some에 넘긴 span이 참조하는 버퍼 목록도 완료 처리까지 보관한다.
    std::vector<asio::const_buffer> m_sendBuffers;
    size_t m_sendOffset = 0; // 현재 배치에서 부분 쓰기로 이미 보낸 바이트 수

    std::atomic<int32> m_recvDelayMs{ 0 };       // 패킷 지연 테스트용 수신 지연 설정(ms)
    std::atomic<int32> m_sendDelayMs{ 0 };       // 패킷 지연 테스트용 송신 지연 설정(ms)
    bool m_bAnnounced = false; // OnConnect 단계까지 진행한 세션에 OnDisconnect를 전달하기 위한 표시
};

using SessionPtr = std::shared_ptr<Session>;

} // namespace netlib
