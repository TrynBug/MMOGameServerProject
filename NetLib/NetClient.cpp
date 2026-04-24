#include "pch.h"
#include "NetClient.h"
#include "IoContext.h"
#include "Session.h"
#include "OverlappedEx.h"
#include "PacketPool.h"
#include "ISession.h"
#include "INetEventHandler.h"
#include "NetLibStats.h"

#include <ws2tcpip.h>
#include <mswsock.h>
#include <chrono>

namespace netlib
{


NetClient::NetClient(IoContext* ioContext)
    : m_ioContext(ioContext)
{
}

NetClient::~NetClient()
{
    Shutdown();
}

// 초기화
bool NetClient::Initialize(const NetClientConfig& config)
{
    if (m_ioContext == nullptr || !m_ioContext->IsRunning())
    {
        return false;
    }

    m_config = config;
    return true;
}

// 종료
void NetClient::Shutdown()
{
    Disconnect();
}

// 연결 시도. 실패하면 자동으로 재연결 한다. 기존 연결이 있으면 끊고 새로 연결한다.
bool NetClient::Connect(const std::string& ip, uint16 port)
{
    m_remoteIp   = ip;
    m_remotePort = port;
    m_bShouldConnect.store(true);
    m_reconnectAttempts.store(0);

    // 재연결 스레드가 아직 안 돌고 있으면 시작
    bool expected = false;
    if (m_bReconnectRunning.compare_exchange_strong(expected, true))
    {
        m_reconnectThread = std::thread(&NetClient::reconnectThreadProc, this);
    }
    else
    {
        // 이미 돌고 있으면 바로 시도하도록 깨움
        m_reconnectCv.notify_all();
    }

    return true;
}

// 연결끊기
void NetClient::Disconnect()
{
    m_bShouldConnect.store(false);

    // 재연결 스레드 종료
    if (m_bReconnectRunning.exchange(false))
    {
        m_reconnectCv.notify_all();
        if (m_reconnectThread.joinable())
        {
            m_reconnectThread.join();
        }
    }

    // 현재 세션 끊기
    SessionPtr spSession;
    {
        std::lock_guard<std::mutex> lock(m_sessionMutex);
        spSession = m_session;
        m_session.reset();
    }

    if (spSession)
    {
        spSession->Disconnect();
    }
}

std::shared_ptr<Session> NetClient::GetSession()
{
    std::lock_guard<std::mutex> lock(m_sessionMutex);
    return m_session;
}

bool NetClient::IsConnected()
{
    std::lock_guard<std::mutex> lock(m_sessionMutex);
    return m_session && m_session->IsConnected();
}

PacketPool& NetClient::GetPacketPool()
{
    return m_ioContext->GetPacketPool();
}

int32 NetClient::GetMaxPacketSize() const
{
    return m_ioContext->GetConfig().maxPacketSize;
}

void NetClient::OnSessionDisconnected(std::shared_ptr<ISession> spSession)
{
    if (m_eventHandler != nullptr)
    {
        m_eventHandler->OnDisconnect(spSession);
    }

    // 활성 세션 제거
    {
        std::lock_guard<std::mutex> lock(m_sessionMutex);
        if (m_session && spSession && m_session->GetId() == spSession->GetId())
        {
            m_session.reset();
        }
    }

    // 자동 재연결이 켜져 있고 Connect가 여전히 요청 상태라면 재시도 깨우기
    if (m_config.bAutoReconnect && m_bShouldConnect.load())
    {
        m_reconnectCv.notify_all();
    }
}

// 재연결 스레드
void NetClient::reconnectThreadProc()
{
    while (m_bReconnectRunning.load())
    {
        // 현재 연결 상태 확인
        bool connected = false;
        {
            std::lock_guard<std::mutex> lock(m_sessionMutex);
            connected = (m_session && m_session->IsConnected());
        }

		// 아직 연결이 안되었고, 연결을 시도 해야하면 postConnect 시도
        if (!connected && m_bShouldConnect.load())
        {
            // 재시도 횟수 제한 체크
            const int32 attempts = m_reconnectAttempts.fetch_add(1);
            if (m_config.maxReconnectAttempts >= 0 && attempts >= m_config.maxReconnectAttempts)
            {
                m_bShouldConnect.store(false);
                if (m_eventHandler != nullptr)
                {
                    m_eventHandler->OnError(nullptr, "Max reconnect attempts reached");
                }
                break;
            }

            // postConnect 성공 시 연결 완료통지는 비동기로 오고, 실패면 아래 wait 후 재시도
            postConnect();
            
        }

        // m_config.reconnectIntervalMs 동안 wait한다. wait이 끝나거나 m_bReconnectRunning이 false로 바뀌었을때 다시 깨어난다.
        std::unique_lock<std::mutex> lock(m_reconnectMutex);
        m_reconnectCv.wait_for(lock, std::chrono::milliseconds(m_config.reconnectIntervalMs),
            [this]()
            {
                return !m_bReconnectRunning.load();
            });
    }
}

// 소켓옵션 설정
void NetClient::setSocketOptions(SOCKET s)
{
    // nagle 알고리즘 설정
    BOOL nagle = m_config.bUseNagle ? FALSE : TRUE;
    ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nagle), sizeof(nagle));

    // SO_LINGER 옵션을 설정해서 소켓을 닫을 때 4-way handshake를 하지 않고 RST 패킷을 바로 보내도록 한다.
    linger lg{};
    lg.l_onoff  = 1;
    lg.l_linger = 0;
    ::setsockopt(s, SOL_SOCKET, SO_LINGER, reinterpret_cast<const char*>(&lg), sizeof(lg));
}

// 실제 ConnectEx 시작
bool NetClient::postConnect()
{
    // 이전 세션이 남아있다면 정리
    {
        SessionPtr spOldSession;
        {
            std::lock_guard<std::mutex> lock(m_sessionMutex);
            spOldSession = m_session;
            m_session.reset();
        }

        if (spOldSession)
        {
            spOldSession->Disconnect();
        }
    }

	// 새 소켓 생성
    SOCKET sock = ::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (sock == INVALID_SOCKET)
    {
        return false;
    }

    setSocketOptions(sock);

    // ConnectEx는 bind된 소켓이 필요하다
    sockaddr_in localAddr{};
    localAddr.sin_family      = AF_INET;
    localAddr.sin_addr.s_addr = INADDR_ANY;
    localAddr.sin_port        = 0;
    if (::bind(sock, reinterpret_cast<sockaddr*>(&localAddr), sizeof(localAddr)) == SOCKET_ERROR)
    {
        ::closesocket(sock);
        return false;
    }

    // ConnectEx 함수 포인터 획득
    LPFN_CONNECTEX fnConnectEx = loadConnectEx(sock);
    if (fnConnectEx == nullptr)
    {
        ::closesocket(sock);
        return false;
    }

	// 연결할 원격 주소 설정
    sockaddr_in remoteAddr{};
    remoteAddr.sin_family = AF_INET;
    remoteAddr.sin_port   = ::htons(m_remotePort);
    if (::inet_pton(AF_INET, m_remoteIp.c_str(), &remoteAddr.sin_addr) != 1)
    {
        ::closesocket(sock);
        return false;
    }

    // Session 생성
    const int64 sessionId = m_nextSessionId.fetch_add(1);
    auto spSession = std::make_shared<Session>(this, sessionId, sock, m_remoteIp, m_remotePort, m_config.recvBufSize);
    spSession->SetConnected(false); // 아직 연결된건 아니라서 m_bConnected=false로 설정

    // IOCP에 등록
    if (!m_ioContext->RegisterSocket(sock, reinterpret_cast<ULONG_PTR>(spSession.get())))
    {
        return false;
    }

    // 세션 등록
    {
        std::lock_guard<std::mutex> lock(m_sessionMutex);
        m_session = spSession;
    }

    // ConnectEx 호출
    OVERLAPPED_EX& overlapped = spSession->GetConnectOverlapped();
    overlapped.Reset();
    overlapped.ioType    = IO_TYPE::Connect;
    overlapped.spSession = spSession;

    DWORD bytesSent = 0;
    const BOOL ret = fnConnectEx(sock,
                                reinterpret_cast<sockaddr*>(&remoteAddr),
                                sizeof(remoteAddr),
                                nullptr, 0, &bytesSent,
                                &overlapped.overlapped);

    NetLibStats::Inc(StatCounter::ConnectPosted);

    // 오류 체크
    if (ret == FALSE)
    {
        const int error = ::WSAGetLastError();
        if (error != WSA_IO_PENDING)
        {
            NetLibStats::Inc(StatCounter::ConnectFailed);
            overlapped.spSession.reset();
            {
                std::lock_guard<std::mutex> lock(m_sessionMutex);
                m_session.reset();
            }
            ::closesocket(sock);

            if (m_eventHandler != nullptr)
            {
                m_eventHandler->OnError(nullptr, "ConnectEx failed immediately");
            }
            return false;
        }
    }

    // 완료통지가 오면 Session::OnConnectCompleted 가 호출됨
    return true;
}

// ConnectEx 함수 포인터를 획득한다. ConnectEx는 Winsock2의 확장 함수이므로 WSAIoctl로 함수 포인터를 얻어와야 한다.
LPFN_CONNECTEX NetClient::loadConnectEx(SOCKET s)
{
    LPFN_CONNECTEX fnConnectEx = nullptr;
    GUID   guid = WSAID_CONNECTEX;
    DWORD  bytes = 0;
    if (::WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER,
        &guid, sizeof(guid),
        &fnConnectEx, sizeof(fnConnectEx),
        &bytes, nullptr, nullptr) == SOCKET_ERROR)
    {
        return nullptr;
    }
    return fnConnectEx;
}

} // namespace netlib
