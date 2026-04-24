#include "pch.h"
#include "Session.h"
#include "INetBase.h"
#include "INetEventHandler.h"
#include "PacketHeader.h"
#include "PacketPool.h"
#include "NetLibStats.h"

namespace netlib
{

Session::Session(INetBase* pNetBase, int64 sessionId, SOCKET socket, const std::string& ip, uint16 port, int32 recvBufSize)
    : m_pNetBase(pNetBase)
    , m_sessionId(sessionId)
    , m_socket(socket)
    , m_ip(ip)
    , m_port(port)
{
    m_recvBuf.Initialize(recvBufSize);

    NetLibStats::Inc(StatCounter::SessionCreated);
}

Session::~Session()
{
    if (m_socket != INVALID_SOCKET)
    {
        ::closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }

    NetLibStats::Inc(StatCounter::SessionDestroyed);
}

void Session::Send(PacketPtr& spPacket)
{
    if (!spPacket || !m_bConnected.load())
    {
        return;
    }

    {
        // SendQueue에 패킷을 넣는다.
        std::lock_guard<std::mutex> lock(m_sendMutex);
        m_sendQueue.push(spPacket);
    }

    // 다른 send IO가 진행중이 아닐때만 Send를 시도한다.
    bool expected = false;
    if (m_bSending.compare_exchange_strong(expected, true))
    {
        postSend();
    }
}

void Session::Disconnect()
{
    CloseSocket();
}

// 소켓을 닫는다.
void Session::CloseSocket()
{
    // 중복 닫기 방지
    bool expected = false;
    if (!m_bClosed.compare_exchange_strong(expected, true))
    {
        return;
    }

    m_bConnected.store(false);

    if (m_socket != INVALID_SOCKET)
    {
        // 소켓에 걸린 모든 IO 취소
        ::CancelIoEx(reinterpret_cast<HANDLE>(m_socket), nullptr);
        // 소켓 닫음
        ::shutdown(m_socket, SD_BOTH);
    }

    if (m_pNetBase != nullptr)
    {
        m_pNetBase->OnSessionDisconnected(shared_from_this());
    }
}

// recv 시작
void Session::StartRecv()
{
    postRecv();
}

// recv IO를 요청한다.
bool Session::postRecv()
{
    if (!m_bConnected.load())
    {
        return false;
    }

    // 다른 recv IO가 진행중이라면 요청하지 않는다.
    bool expected = false;
    if (!m_bRecving.compare_exchange_strong(expected, true))
    {
        return false;
    }

    // 수신버퍼 남은크기 확인
    const int32 freeSize = m_recvBuf.GetFreeSize();
    if (freeSize <= 0)
    {
        m_bRecving.store(false);
        NetLibStats::Inc(StatCounter::RecvBufferFull);
        if (m_pNetBase != nullptr && m_pNetBase->GetEventHandler() != nullptr)
        {
            m_pNetBase->GetEventHandler()->OnError(shared_from_this(), "Recv buffer full");
        }
        CloseSocket();
        return false;
    }

    // 수신버퍼를 WSABUF에 입력
    WSABUF WSABufs[2];
    const int32 directFreeSize = m_recvBuf.GetDirectFreeSize();
    WSABufs[0].buf = m_recvBuf.GetRear();   // 링버퍼의 rear 부터 버퍼 끝까지의 공간 등록
    WSABufs[0].len = directFreeSize;
    WSABufs[1].buf = m_recvBuf.GetBuffer();  // 링버퍼의 시작부터 front 까지의 공간 등록
    WSABufs[1].len = m_recvBuf.GetFreeSize() - directFreeSize;

    // 수신용 overlapped 구조체 설정
    m_recvOverlapped.Reset();
    m_recvOverlapped.ioType    = IO_TYPE::Recv;
    m_recvOverlapped.spSession = shared_from_this();
    
    // WSARecv
    DWORD flags = 0;
    DWORD receivedBytes = 0;
    const int ret = ::WSARecv(m_socket, WSABufs, 2, &receivedBytes, &flags, &m_recvOverlapped.overlapped, nullptr);
    NetLibStats::Inc(StatCounter::RecvPosted);
    if (ret == SOCKET_ERROR)
    {
        const int error = ::WSAGetLastError();
        if (error != WSA_IO_PENDING)
        {
            if (error == WSAECONNRESET   ||                              // 클라이언트에서 연결을 끊음
                error == WSAECONNABORTED ||                              // 서버에서 연결을 끊음?
                (error == WSAENOTSOCK && m_socket == INVALID_SOCKET))    // 서버에서 소켓을 닫고 소켓값을 INVALID_SOCKET으로 변경함
            {
                NetLibStats::Inc(StatCounter::RecvKnownFailed);
            }
            else
            {
                INetEventHandler* handler = m_pNetBase->GetEventHandler();
                if (handler != nullptr)
                {
                    handler->OnError(shared_from_this(), std::format("WSARecv failed by unknown error. error:{}, session:{}", error, m_sessionId));
                }

                NetLibStats::Inc(StatCounter::RecvUnknownFailed);
            }

            m_recvOverlapped.spSession.reset();
            m_bRecving.store(false);
            CloseSocket();
            return false;
        }
    }
    return true;
}

// worker 스레드가 recv 완료통지 받았을때 호출해주는 콜백함수
void Session::OnRecvCompleted(DWORD bytesTransferred)
{
    NetLibStats::Inc(StatCounter::RecvCompleted);
    m_recvOverlapped.spSession.reset();

    if (!m_bConnected.load())
    {
        m_bRecving.store(false);
        CloseSocket();
        return;
    }

    // 수신버퍼 rear를 뒤로 밀어서 데이터가 있음을 표시한다.
    m_recvBuf.MoveRear(static_cast<int32>(bytesTransferred));
    m_bRecving.store(false);

    // 수신버퍼의 데이터 확인
    parseReceivedPackets();

    // recv 재시작
    postRecv();
}

// 수신버퍼의 데이터 확인
void Session::parseReceivedPackets()
{
    if (m_pNetBase == nullptr)
    {
        return;
    }
    INetEventHandler* handler = m_pNetBase->GetEventHandler();
    const int32 maxPacketSize = m_pNetBase->GetMaxPacketSize();

    while (m_bConnected.load())
    {
        const int32 usedSize = m_recvBuf.GetUseSize();
        if (usedSize < static_cast<int32>(sizeof(PacketHeader)))
        {
            break;
        }

        // 패킷 헤더 만큼의 데이터를 확인해본다.
        PacketHeader header;
        m_recvBuf.Peek(reinterpret_cast<char*>(&header), sizeof(PacketHeader));

        if (header.size < sizeof(PacketHeader) || header.size > maxPacketSize)
        {
            // 패킷 크기가 너무 큼
            NetLibStats::Inc(StatCounter::InvalidPacketHeader);
            if (handler != nullptr)
            {
                handler->OnError(shared_from_this(), "Invalid packet size");
            }
            CloseSocket();
            return;
        }

        // 패킷 1개 데이터가 모두 도착하지 않았음
        if (usedSize < header.size)
        {
            break;
        }

        // 패킷버퍼 할당받기
        PacketPtr spPacket = m_pNetBase->GetPacketPool().Alloc(header.size);
        if (spPacket == nullptr)
        {
            NetLibStats::Inc(StatCounter::PacketPoolAllocFail);
            if (handler != nullptr)
            {
                handler->OnError(shared_from_this(), "PacketPool alloc failed");
            }
            CloseSocket();
            return;
        }

        // 수신버퍼의 데이터를 꺼내서 패킷버퍼에 담는다.
        m_recvBuf.Dequeue(reinterpret_cast<char*>(spPacket->GetRawBuffer()), header.size);

        // 사용자에게 패킷 전달
        if (handler != nullptr)
        {
            handler->OnRecv(shared_from_this(), spPacket);
        }
    }
}

// send 대기중인 패킷을 전송한다.
bool Session::postSend()
{
    if (!m_bConnected.load())
    {
        m_bSending.store(false);
        return false;
    }

    // sendQueue에서 패킷을 최대 SEND_WSABUF_MAX_SIZE개 까지 꺼낸다.
    m_sendingPackets.clear();
    WSABUF WSABufs[SEND_WSABUF_MAX_SIZE];
    {
        std::lock_guard<std::mutex> lock(m_sendMutex);

        for (size_t i = 0; i < SEND_WSABUF_MAX_SIZE; ++i)
        {
            if (m_sendQueue.empty())
                break;

            PacketPtr spPacket = m_sendQueue.front();
            m_sendQueue.pop();

            WSABufs[i].buf = reinterpret_cast<CHAR*>(spPacket->GetRawBuffer());
            WSABufs[i].len = static_cast<ULONG>(spPacket->GetTotalSize());

            m_sendingPackets.push_back(spPacket);  // 패킷 객체가 사라지는것을 방지하기위해 저장해둠
        }
    }

    if (m_sendingPackets.empty())
    {
        m_bSending.store(false);
        return false;
    }

    // overlapped 초기화
    m_sendOverlapped.Reset();
    m_sendOverlapped.ioType    = IO_TYPE::Send;
    m_sendOverlapped.spSession = shared_from_this();

    // WSASend
    DWORD sentBytes = 0;
    const int ret = ::WSASend(m_socket, WSABufs, static_cast<DWORD>(m_sendingPackets.size()), &sentBytes, 0, &m_sendOverlapped.overlapped, nullptr);
    NetLibStats::Inc(StatCounter::SendPosted);
    if (ret == SOCKET_ERROR)
    {
        const int error = ::WSAGetLastError();
        if (error != WSA_IO_PENDING)
        {
            if (error == WSAECONNRESET ||                              // 클라이언트에서 연결을 끊음
                error == WSAECONNABORTED ||                            // 서버에서 연결을 끊음?
                (error == WSAENOTSOCK && m_socket == INVALID_SOCKET))  // 서버에서 소켓을 닫고 소켓값을 INVALID_SOCKET으로 변경함
            {
                NetLibStats::Inc(StatCounter::SendKnownFailed);
            }
            else
            {
                INetEventHandler* handler = m_pNetBase->GetEventHandler();
                if (handler != nullptr)
                {
                    handler->OnError(shared_from_this(), std::format("WSASend failed by unknown error. error:{}, session:{}", error, m_sessionId));
                }

                NetLibStats::Inc(StatCounter::SendUnknownFailed);
            }

            m_sendOverlapped.spSession.reset();
            m_sendingPackets.clear();
            m_bSending.store(false);
            CloseSocket();
            return false;
        }
    }
    return true;
}

// worker 스레드가 send 완료통지 받았을때 호출해주는 콜백함수
void Session::OnSendCompleted(DWORD bytesTransferred)
{
    (void)bytesTransferred;

    NetLibStats::Inc(StatCounter::SendCompleted);

    m_sendingPackets.clear();
    m_sendOverlapped.spSession.reset();

    if (!m_bConnected.load())
    {
        m_bSending.store(false);
        return;
    }

    trySendNext();
}

// send를 시도한다.
void Session::trySendNext()
{
    bool hasPackets = false;
    {
        std::lock_guard<std::mutex> lock(m_sendMutex);
        hasPackets = !m_sendQueue.empty();
    }

    // 보낼패킷이 있으면 전송
    if (hasPackets)
    {
        postSend();
        return;
    }

    m_bSending.store(false);
}

// worker 스레드가 ConnectEx 완료통지 받았을때 호출해주는 콜백함수 (NetClient에서만 사용)
// @success : false면 연결 실패한 것이다. 그러면 m_pNetBase 쪽에서 재연결을 시도한다.
void Session::OnConnectCompleted(bool success)
{
    NetLibStats::Inc(StatCounter::ConnectCompleted);
    m_connectOverlapped.spSession.reset();

    if (!success)
    {
        NetLibStats::Inc(StatCounter::ConnectFailed);
        CloseSocket();
        return;
    }

    // ConnectEx는 비동기 함수라서 성공 후에 소켓에 SO_UPDATE_CONNECT_CONTEXT 옵션을 설정해야 소켓 컨텍스트가 업데이트됨
    // 설정 안하면 shutdown, getpeername 함수 등이 제대로 작동안함
    ::setsockopt(m_socket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0);

    m_bConnected.store(true);

    // OnConnect 콜백
    if (m_pNetBase != nullptr && m_pNetBase->GetEventHandler() != nullptr)
    {
        m_pNetBase->GetEventHandler()->OnConnect(shared_from_this());
    }

    // recv 시작
    StartRecv();
}

} // namespace netlib
