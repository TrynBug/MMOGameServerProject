#include "Session.h"
#include "../NetLibCommon/NetLibStats.h"
#include "../NetLibCommon/PacketPool.h"

#include <algorithm>
#include <array>
#include <span>

namespace netlib
{

namespace
{

constexpr size_t SEND_BUFFER_MAX_SIZE = 50;
// 워커가 마지막 송신 요청을 가져간 시점부터 기다리는 시간이다.
constexpr auto DISCONNECT_SEND_TIMEOUT = std::chrono::seconds(5);

bool isKnownError(const ErrorCode& error)
{
    return error == asio::error::connection_reset || error == asio::error::connection_aborted ||
           error == asio::error::broken_pipe || error == asio::error::not_connected ||
           error == asio::error::operation_aborted || error == asio::error::eof;
}

} // namespace

Session::Session(asio::io_context& context, PacketPool& packetPool, int32 maxPacketSize,
                 int64 sessionId, Tcp::socket socket, const Tcp::endpoint& remote,
                 int32 recvBufSize, int32 recvPacketReserveSize,
                 std::shared_ptr<EventHandlerSlot> spHandler, std::function<void(int64)> onDisconnect)
    : m_pContext(&context)
    , m_packetPool(packetPool)
    , m_maxPacketSize(maxPacketSize)
    , m_sessionId(sessionId)
    , m_ip(remote.address().to_string())
    , m_port(remote.port())
    , m_recvPacketReserveSize(recvPacketReserveSize)
    , m_socket(std::make_unique<Tcp::socket>(std::move(socket)))
    , m_spHandler(std::move(spHandler))
    , m_onDisconnect(std::move(onDisconnect))
{
    m_recvBuf.Initialize(recvBufSize);
    NetLibStats::Inc(StatCounter::SessionCreated);
}

Session::~Session()
{
    NetLibStats::Add(StatGauge::RecvBufferUsedBytes, -static_cast<int64>(m_recvBuf.GetUseSize()));
    NetLibStats::Inc(StatCounter::SessionDestroyed);
}

void Session::SetSocketOptions(Tcp::socket& socket, bool bUseNagle)
{
    ErrorCode error;
    socket.set_option(Tcp::no_delay(!bUseNagle), error);
    socket.set_option(asio::socket_base::linger(true, 0), error);
}

void Session::Start(bool bAccept)
{
    std::lock_guard lock(m_sessionMutex);
    if (!m_socket)
        return;
    m_bConnected.store(true);
    NetLibStats::Add(StatGauge::ActiveSessions, 1);

    // 수락 여부를 먼저 결정한 뒤 연결을 통지하고, 그 다음에 첫 수신을 등록한다.
    auto* handler = m_spHandler->load();
    if (bAccept && handler && !handler->OnAccept(shared_from_this()))
    {
        Close();
        return;
    }
    if (!IsConnected())
        return;

    if (bAccept)
        NetLibStats::Inc(StatCounter::AcceptCount);
    m_bAnnounced = true;
    if (handler)
        handler->OnConnect(shared_from_this());
    postRecv();
}

void Session::Send(const PacketPtr& spPacket)
{
    queueSend(spPacket, false);
}

void Session::SendAndDisconnect(const PacketPtr& spPacket)
{
    // 직렬화 실패 등으로 보낼 패킷이 없으면 즉시 종료를 요청한다.
    if (spPacket)
        queueSend(spPacket, true);
    else
        Disconnect();
}

void Session::queueSend(const PacketPtr& spPacket, bool bDisconnectAfterSend)
{
    if (!spPacket)
        return;

    std::lock_guard lock(m_postMutex);
    if (!IsConnected() || m_bDisconnectAfterSend)
        return;

    // 패킷 지연 테스트용. Send() 호출 시점의 설정으로 전달 예정 시각을 계산한다.
    const int32 delayMs = m_sendDelayMs.load();
    const auto deliverAt = delayMs > 0
        ? std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs)
        : std::chrono::steady_clock::time_point{};
    m_pendingSends.emplace_back(deliverAt, spPacket);
    // 마지막 패킷 등록과 새 Send() 차단을 같은 락으로 묶는다.
    // 기존 대기열을 비울 때까지 IsConnected()는 true로 유지한다.
    m_bDisconnectAfterSend = bDisconnectAfterSend;
    // 대기열이 비어 있다가 첫 패킷이 들어올 때만 워커 작업을 게시한다.
    if (m_pendingSends.size() != 1)
        return;

    asio::post(*m_pContext, [self = shared_from_this()]()
    {
        // 대기열을 비우기 전에 세션 락을 잡고, 가져온 패킷을 모두 분류할 때까지 유지한다.
        // 새 작업이 게시돼도 다음 워커는 이 락을 기다리므로 앞선 패킷을 추월할 수 없다.
        std::lock_guard sessionLock(self->m_sessionMutex);
        std::deque<DelayedPacket> pending;
        bool bDisconnectAfterSend;
        {
            std::lock_guard postLock(self->m_postMutex);
            // 마지막 송신 플래그와 해당 패킷을 같은 시점에 인수한다.
            pending.swap(self->m_pendingSends);
            bDisconnectAfterSend = self->m_bDisconnectAfterSend;
        }
        if (!self->IsConnected())
            return;
        if (bDisconnectAfterSend && !self->m_disconnectTimer)
        {
            // 이 작업이 마지막 패킷까지 인수했다. 상대가 읽지 않을 때는 제한 시간 뒤 닫는다.
            self->m_disconnectTimer = std::make_unique<asio::steady_timer>(*self->m_pContext);
            self->m_disconnectTimer->expires_after(DISCONNECT_SEND_TIMEOUT);
            self->m_disconnectTimer->async_wait([self](const ErrorCode& error)
            {
                if (!error)
                    self->Close();
            });
        }
        for (auto& packet : pending)
        {
            // 패킷 지연 테스트용. 이미 지연 큐에서 기다리는 패킷이 있으면 그 뒤에 붙인다.
            if (packet.first > std::chrono::steady_clock::now() ||
                (self->m_delayedSends && !self->m_delayedSends->packets.empty()))
            {
                if (!self->m_delayedSends)
                    self->m_delayedSends = std::make_unique<DelayQueue>(*self->m_pContext);
                self->queueDelayedPacket(*self->m_delayedSends, std::move(packet), true);
            }
            else
                self->enqueueSend(packet.second);
        }
        self->startSend();
    });
}

void Session::Disconnect()
{
    std::lock_guard lock(m_postMutex);
    // 먼저 종료 요청 상태로 바꿔 추가 송신을 막고, 실제 소켓 종료는 워커에 맡긴다.
    if (IsConnected() && !m_bDisconnectRequested.exchange(true))
        asio::post(*m_pContext, [self = shared_from_this()]() { self->Close(); });
}

void Session::Close()
{
    std::lock_guard lock(m_sessionMutex);
    if (!m_socket)
        return;

    {
        std::lock_guard postLock(m_postMutex);
        if (m_bConnected.exchange(false))
            NetLibStats::Add(StatGauge::ActiveSessions, -1);
        m_pendingSends.clear();
    }

    ErrorCode error;
    m_socket->close(error);
    m_socket.reset();
    // 정상적으로 마지막 쓰기가 끝났거나 먼저 연결이 끊겼으면 종료 타이머를 취소한다.
    m_disconnectTimer.reset();
    // 패킷 지연 테스트용 타이머를 취소하고 아직 전달하지 않은 패킷을 해제한다.
    m_delayedRecvs.reset();
    m_delayedSends.reset();
    for (const auto& spPacket : m_sendQueue)
        NetLibStats::Add(StatGauge::SendQueueBytes, -spPacket->GetTotalSize());
    NetLibStats::Add(StatGauge::SendQueuePackets, -static_cast<int64>(m_sendQueue.size()));
    m_sendQueue.clear();

    // 진행 중인 송신 버퍼는 취소 완료 핸들러까지 유지한다.
    // OnConnect 단계에 진입했던 세션에만 OnDisconnect를 통지한다.
    if (m_bAnnounced)
    {
        if (auto* handler = m_spHandler->load())
            handler->OnDisconnect(shared_from_this());
    }
    m_onDisconnect(m_sessionId);
}

void Session::postRecv()
{
    if (!IsConnected())
        return;

    const int32 freeSize = m_recvBuf.GetFreeSize();
    if (freeSize <= 0)
    {
        NetLibStats::Inc(StatCounter::RecvBufferFull);
        logError("Recv buffer full");
        Close();
        return;
    }

    // 링버퍼 끝을 넘어갈 때도 빈 구간 두 곳에 한 번의 read로 이어서 받는다.
    const int32 directSize = m_recvBuf.GetDirectFreeSize();
    const std::array buffers{
        asio::buffer(m_recvBuf.GetRear(), directSize),
        asio::buffer(m_recvBuf.GetBuffer(), freeSize - directSize)
    };
    NetLibStats::Inc(StatCounter::RecvPosted);
    m_socket->async_read_some(buffers, [self = shared_from_this()](const ErrorCode& error, size_t bytesTransferred)
    {
        std::lock_guard lock(self->m_sessionMutex);
        if (!self->IsConnected())
            return;
        if (error)
        {
            self->closeOnError(error, false);
            return;
        }
        NetLibStats::Inc(StatCounter::RecvCompleted);
        NetLibStats::Inc(StatCounter::RecvBytes, bytesTransferred);
        NetLibStats::Add(StatGauge::RecvBufferUsedBytes, static_cast<int64>(bytesTransferred));
        self->m_recvBuf.MoveRear(static_cast<int32>(bytesTransferred));
        self->parseReceivedPackets();
        // 첫 수신 이후에는 완료 콜백에서 다음 읽기를 하나씩 등록한다.
        self->postRecv();
    });
}

void Session::parseReceivedPackets()
{
    // TCP 바이트 스트림에서 완성된 패킷만 꺼낸다. 남은 조각은 다음 read와 합친다.
    while (IsConnected() && m_recvBuf.GetUseSize() >= static_cast<int32>(sizeof(PacketHeader)))
    {
        PacketHeader header;
        m_recvBuf.Peek(reinterpret_cast<char*>(&header), sizeof(header));
        if (header.size < sizeof(PacketHeader) || header.size > m_maxPacketSize - m_recvPacketReserveSize)
        {
            NetLibStats::Inc(StatCounter::InvalidPacketHeader);
            logError("Invalid packet size");
            Close();
            return;
        }
        if (m_recvBuf.GetUseSize() < header.size)
            return;

        // 상위 코드가 Sidecar를 추가할 수 있도록 패킷 길이와 별도로 버퍼 여유를 확보한다.
        PacketPtr spPacket = m_packetPool.Alloc(static_cast<int32>(header.size) + m_recvPacketReserveSize);
        if (!spPacket)
        {
            logError("PacketPool alloc failed");
            Close();
            return;
        }
        m_recvBuf.Dequeue(reinterpret_cast<char*>(spPacket->GetRawBuffer()), header.size);
        NetLibStats::Add(StatGauge::RecvBufferUsedBytes, -static_cast<int64>(header.size));
        NetLibStats::Inc(StatCounter::RecvPackets);

        // 패킷 지연 테스트용. TCP 수신과 패킷 조립은 끝났고 OnRecv 호출만 늦춘다.
        const int32 delayMs = m_recvDelayMs.load();
        if (delayMs > 0 || (m_delayedRecvs && !m_delayedRecvs->packets.empty()))
        {
            if (!m_delayedRecvs)
                m_delayedRecvs = std::make_unique<DelayQueue>(*m_pContext);
            const auto deliverAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs);
            queueDelayedPacket(*m_delayedRecvs, { deliverAt, spPacket }, false);
        }
        else
            deliverPacket(spPacket);
    }
}

void Session::enqueueSend(const PacketPtr& spPacket)
{
    NetLibStats::Add(StatGauge::SendQueuePackets, 1);
    NetLibStats::Add(StatGauge::SendQueueBytes, spPacket->GetTotalSize());
    m_sendQueue.push_back(spPacket);
}

void Session::startSend()
{
    // 세션마다 쓰기 배치 하나만 진행한다. 완료되면 대기열의 다음 패킷을 모은다.
    if (!IsConnected() || !m_sendingPackets.empty())
        return;

    if (m_sendQueue.empty())
    {
        // 진행 중인 쓰기가 없고 패킷 지연 테스트용 송신 큐까지 비었을 때 닫는다.
        if (m_disconnectTimer && (!m_delayedSends || m_delayedSends->packets.empty()))
        {
            // 소켓에 설정된 즉시 종료용 linger를 해제하고, 마지막 데이터 뒤에 TCP FIN을 보낸다.
            ErrorCode error;
            m_socket->set_option(asio::socket_base::linger(false, 0), error);
            m_socket->shutdown(Tcp::socket::shutdown_send, error);
            Close();
        }
        return;
    }

    while (!m_sendQueue.empty() && m_sendingPackets.size() < SEND_BUFFER_MAX_SIZE)
    {
        auto spPacket = std::move(m_sendQueue.front());
        m_sendQueue.pop_front();
        const int64 bytes = spPacket->GetTotalSize();
        NetLibStats::Add(StatGauge::SendQueuePackets, -1);
        NetLibStats::Add(StatGauge::SendQueueBytes, -bytes);
        NetLibStats::Add(StatGauge::SendInFlightPackets, 1);
        NetLibStats::Add(StatGauge::SendInFlightBytes, bytes);
        m_sendingPackets.push_back(std::move(spPacket));
    }
    postSend();
}

void Session::postSend()
{
    // 배치 내 패킷 버퍼를 그대로 참조하고, 부분 쓰기에서는 이미 전송한 바이트만 건너뛴다.
    m_sendBuffers.clear();
    m_sendBuffers.reserve(m_sendingPackets.size());
    size_t skip = m_sendOffset;
    size_t remaining = 0;
    for (const auto& spPacket : m_sendingPackets)
    {
        const size_t size = spPacket->GetTotalSize();
        if (skip >= size)
        {
            skip -= size;
            continue;
        }
        m_sendBuffers.push_back(asio::buffer(spPacket->GetRawBuffer() + skip, size - skip));
        remaining += size - skip;
        skip = 0;
    }

    NetLibStats::Inc(StatCounter::SendPosted);
    const auto bufferSequence = std::span<const asio::const_buffer>{ m_sendBuffers };
    m_socket->async_write_some(bufferSequence, [self = shared_from_this(), remaining](const ErrorCode& error, size_t bytesTransferred)
    {
        std::lock_guard lock(self->m_sessionMutex);
        if (!self->IsConnected() || error)
        {
            self->clearSendingPackets();
            if (error && self->IsConnected())
                self->closeOnError(error, true);
            return;
        }
        NetLibStats::Inc(StatCounter::SendCompleted);
        NetLibStats::Inc(StatCounter::SendBytes, bytesTransferred);
        if (bytesTransferred < remaining)
        {
            self->m_sendOffset += bytesTransferred;
            self->postSend();
            return;
        }

        // 전송 바이트는 write 완료마다 집계하고, 패킷 수와 완료 이벤트는 배치 단위로 처리한다.
        NetLibStats::Inc(StatCounter::SendPackets, self->m_sendingPackets.size());
        self->clearSendingPackets();
        if (auto* handler = self->m_spHandler->load())
            handler->OnSendComplete(self);
        self->startSend();
    });
}

void Session::clearSendingPackets()
{
    for (const auto& spPacket : m_sendingPackets)
        NetLibStats::Add(StatGauge::SendInFlightBytes, -spPacket->GetTotalSize());
    NetLibStats::Add(StatGauge::SendInFlightPackets, -static_cast<int64>(m_sendingPackets.size()));
    m_sendBuffers.clear();
    m_sendingPackets.clear();
    m_sendOffset = 0;
}

// 패킷 지연 테스트용 설정. 이미 예약된 패킷은 기존 전달 순서를 유지한다.
void Session::SetSimulatedDelay(int32 recvMs, int32 sendMs)
{
    m_recvDelayMs.store(std::max(0, recvMs));
    m_sendDelayMs.store(std::max(0, sendMs));
}

// 패킷 지연 테스트용 큐에 패킷을 추가한다. 세션 락을 잡은 상태에서 호출한다.
void Session::queueDelayedPacket(DelayQueue& queue, DelayedPacket packet, bool bSend)
{
    const bool bEmpty = queue.packets.empty();
    // 지연을 줄여도 새 패킷의 전달 시각을 앞 패킷보다 이르게 잡지 않는다.
    if (!bEmpty)
        packet.first = std::max(packet.first, queue.packets.back().first);
    queue.packets.push_back(std::move(packet));
    if (bEmpty)
        armDelayTimer(queue, bSend);
}

// 패킷 지연 테스트용. 방향별 타이머 하나로 큐 맨 앞의 전달 시각을 기다린다.
void Session::armDelayTimer(DelayQueue& queue, bool bSend)
{
    queue.timer.expires_at(queue.packets.front().first);
    queue.timer.async_wait([self = shared_from_this(), bSend](const ErrorCode& error)
    {
        std::lock_guard lock(self->m_sessionMutex);
        auto& queue = bSend ? self->m_delayedSends : self->m_delayedRecvs;
        if (error || !self->IsConnected())
            return;

        // 워커가 깨어날 때 이미 전달 시각이 지난 패킷은 한 번에 순서대로 처리한다.
        // 송신 패킷은 소켓 쓰기 대기열에 넣고, 수신 패킷은 OnRecv에 전달한다.
        while (self->IsConnected() && !queue->packets.empty() &&
               queue->packets.front().first <= std::chrono::steady_clock::now())
        {
            auto spPacket = std::move(queue->packets.front().second);
            queue->packets.pop_front();
            if (bSend)
                self->enqueueSend(spPacket);
            else
                self->deliverPacket(spPacket);
        }
        if (!self->IsConnected())
            return;
        // 아직 전달할 시간이 되지 않은 패킷은 다음 만료 시각에 이어서 처리한다.
        if (!queue->packets.empty())
            self->armDelayTimer(*queue, bSend);
        // 마지막 송신이 끝나면 startSend()가 지연 큐까지 해제할 수 있으므로 끝에서 호출한다.
        if (bSend)
            self->startSend();
    });
}

void Session::deliverPacket(const PacketPtr& spPacket)
{
    if (auto* handler = m_spHandler->load())
        handler->OnRecv(shared_from_this(), spPacket);
}

void Session::closeOnError(const ErrorCode& error, bool bSend)
{
    if (!bSend && error == asio::error::eof)
    {
        NetLibStats::Inc(StatCounter::GracefulDisconnect);
        // 상대가 먼저 송신 방향을 닫아도 예약한 마지막 응답은 계속 보낼 수 있다.
        std::lock_guard lock(m_postMutex);
        if (m_bDisconnectAfterSend)
            return;
    }
    else
    {
        NetLibStats::Inc(StatCounter::AbnormalDisconnect);
        const bool bKnown = isKnownError(error);
        NetLibStats::Inc(bSend
            ? (bKnown ? StatCounter::SendKnownFailed : StatCounter::SendUnknownFailed)
            : (bKnown ? StatCounter::RecvKnownFailed : StatCounter::RecvUnknownFailed));
        if (!bKnown)
            logError(std::string(bSend ? "Send failed: " : "Recv failed: ") + error.message());
    }
    Close();
}

void Session::logError(const std::string& message)
{
    if (auto* handler = m_spHandler->load())
        handler->OnLog(LogLevel::Error, shared_from_this(), message);
}

} // namespace netlib
