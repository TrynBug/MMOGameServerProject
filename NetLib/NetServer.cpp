#include "pch.h"
#include "NetServer.h"
#include "IoContext.h"
#include "Session.h"
#include "OverlappedEx.h"
#include "PacketPool.h"
#include "ISession.h"
#include "INetEventHandler.h"
#include "NetLibStats.h"

#include <ws2tcpip.h>
#include <vector>

namespace netlib
{

NetServer::NetServer(IoContext* pIoContext)
    : m_pIoContext(pIoContext)
{
}

NetServer::~NetServer()
{
    Shutdown();
}


bool NetServer::Initialize(const NetServerConfig& config)
{
    if (m_pIoContext == nullptr || !m_pIoContext->IsRunning())
    {
        return false;
    }
    m_config = config;
    return true;
}

// Accept 시작
bool NetServer::StartAccept()
{
    if (m_bAccepting.load())
    {
        return false;
    }

    m_listenSocket = ::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (m_listenSocket == INVALID_SOCKET)
    {
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = ::htons(m_config.port);
    if (m_config.ip.empty() || m_config.ip == "0.0.0.0")
    {
        addr.sin_addr.s_addr = INADDR_ANY;
    }
    else
    {
        ::inet_pton(AF_INET, m_config.ip.c_str(), &addr.sin_addr);
    }

    if (::bind(m_listenSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR)
    {
        ::closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
        return false;
    }

    if (::listen(m_listenSocket, m_config.backlog) == SOCKET_ERROR)
    {
        ::closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
        return false;
    }

    m_bAccepting.store(true);
    m_acceptThread = std::thread(&NetServer::acceptThreadProc, this);
    return true;
}

// Accept 중지
void NetServer::StopAccept()
{
    if (!m_bAccepting.exchange(false))
    {
        return;
    }

    if (m_listenSocket != INVALID_SOCKET)
    {
        ::closesocket(m_listenSocket);
        m_listenSocket = INVALID_SOCKET;
    }

    if (m_acceptThread.joinable())
    {
        m_acceptThread.join();
    }
}

// 서버 종료
void NetServer::Shutdown()
{
    StopAccept();

    std::vector<SessionPtr> sessions;
    {
        std::lock_guard<std::mutex> lock(m_sessionMutex);
        sessions.reserve(m_sessions.size());
        for (auto& [sessionId, spSession] : m_sessions)
        {
            sessions.push_back(spSession);
        }
    }

	// 모든 세션 종료. 세션 연결 끊길 때 OnSessionDisconnected 콜백이 호출되고, 여기서 세션이 m_sessions에서 제거된다.
    for (SessionPtr& spSession : sessions)
    {
        spSession->Disconnect();
    }

    {
        std::lock_guard<std::mutex> lock(m_sessionMutex);
        m_sessions.clear();
    }
}

// 세션 찾기
std::shared_ptr<Session> NetServer::FindSession(int64 sessionId)
{
    std::lock_guard<std::mutex> lock(m_sessionMutex);
    auto iter = m_sessions.find(sessionId);
    if(iter == m_sessions.end())
    {
        return nullptr;
	}

    return iter->second;
}

// 세션 제거
void NetServer::RemoveSession(int64 sessionId)
{
    std::lock_guard<std::mutex> lock(m_sessionMutex);
    m_sessions.erase(sessionId);
}

int32 NetServer::GetSessionCount()
{
    std::lock_guard<std::mutex> lock(m_sessionMutex);
    return static_cast<int32>(m_sessions.size());
}

PacketPool& NetServer::GetPacketPool()
{
    return m_pIoContext->GetPacketPool();
}

int32 NetServer::GetMaxPacketSize() const
{
    return m_pIoContext->GetConfig().maxPacketSize;
}

// 세션 연결끊김 콜백
void NetServer::OnSessionDisconnected(std::shared_ptr<ISession> spSession)
{
    if (m_eventHandler != nullptr)
    {
        m_eventHandler->OnDisconnect(spSession);
    }

    if (spSession != nullptr)
    {
        RemoveSession(spSession->GetId());
    }
}

// 세션ID 생성. 세션ID는 단순히 1씩 증가한다.
int64 NetServer::generateSessionId()
{
    return m_nextSessionId.fetch_add(1);
}

void NetServer::setSocketOptions(SOCKET s)
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


// Accept 스레드
void NetServer::acceptThreadProc()
{
    while (m_bAccepting.load())
    {
        sockaddr_in clientAddr{};
        int         addrLen = sizeof(clientAddr);

        SOCKET clientSocket = ::accept(m_listenSocket, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
        if (clientSocket == INVALID_SOCKET)
        {
            if (!m_bAccepting.load())
            {
                break;
            }
            continue;
        }

        char ipBuf[INET_ADDRSTRLEN] = {0};
        ::inet_ntop(AF_INET, &clientAddr.sin_addr, ipBuf, sizeof(ipBuf));
        const std::string ip   = ipBuf;
        const uint16      port = ::ntohs(clientAddr.sin_port);

        if (!registerNewSession(clientSocket, ip, port))
        {
            ::closesocket(clientSocket);
        }
    }
}

bool NetServer::registerNewSession(SOCKET clientSocket, const std::string& ip, uint16 port)
{
    setSocketOptions(clientSocket);

    const int64 sessionId = generateSessionId();
    auto spSession = std::make_shared<Session>(
        this, sessionId, clientSocket, ip, port, m_config.recvBufSize);

    spSession->SetConnected(true);

    if (m_eventHandler != nullptr)
    {
        if (!m_eventHandler->OnAccept(spSession))
        {
            return false;
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_sessionMutex);
        m_sessions[sessionId] = spSession;
    }

    if (!m_pIoContext->RegisterSocket(clientSocket,
                                      reinterpret_cast<ULONG_PTR>(spSession.get())))
    {
        std::lock_guard<std::mutex> lock(m_sessionMutex);
        m_sessions.erase(sessionId);
        return false;
    }

    // 등록이 완전히 성공한 시점에 AcceptCount 증가.
    NetLibStats::Inc(StatCounter::AcceptCount);

    if (m_eventHandler != nullptr)
    {
        m_eventHandler->OnConnect(spSession);
    }

    spSession->StartRecv();
    return true;
}

} // namespace netlib
