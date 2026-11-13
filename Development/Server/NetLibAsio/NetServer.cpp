#include "NetServer.h"
#include "IoContext.h"
#include "Session.h"

#include <unordered_map>

namespace netlib
{

struct NetServer::State : std::enable_shared_from_this<State>
{
    explicit State(IoContext* pIoContext) : pIoContext(pIoContext) {}

    void postAccept()
    {
        // 취소 완료까지 State를 유지하고, 리스너가 재시작되면 이전 accept 결과는 무시한다.
        acceptor->async_accept([self = shared_from_this(), generation = acceptGeneration](const ErrorCode& error, Tcp::socket socket)
        {
            std::lock_guard lock(self->acceptMutex);
            if (!self->acceptor || generation != self->acceptGeneration || !self->pIoContext->IsRunning())
                return;
            if (!error)
                self->registerSession(std::move(socket));
            else if (auto* handler = self->spHandler->load())
                handler->OnLog(LogLevel::Error, nullptr, "Accept failed: " + error.message());

            // 사용자 콜백에서 리스너를 중지하거나 재시작할 수 있으므로 다시 확인한다.
            if (self->acceptor && generation == self->acceptGeneration && self->pIoContext->IsRunning())
                self->postAccept();
        });
    }

    void registerSession(Tcp::socket socket)
    {
        ErrorCode error;
        const auto remote = socket.remote_endpoint(error);
        if (error)
            return;
        Session::SetSocketOptions(socket, config.bUseNagle);

        const int64 sessionId = nextSessionId++;
        auto spSession = std::make_shared<Session>(
            pIoContext->getContext(), pIoContext->GetPacketPool(), pIoContext->GetConfig().maxPacketSize,
            sessionId, std::move(socket), remote, config.recvBufSize, config.recvPacketReserveSize,
            spHandler, [weak = weak_from_this()](int64 id)
            {
                if (auto self = weak.lock())
                {
                    std::lock_guard lock(self->sessionMutex);
                    self->sessions.erase(id);
                }
            });
        // OnAccept와 OnConnect 실행 중에도 세션 ID로 조회할 수 있도록 먼저 등록한다.
        {
            std::lock_guard lock(sessionMutex);
            sessions.emplace(sessionId, spSession);
        }
        spSession->Start(true);
    }

    void shutdown()
    {
        {
            std::lock_guard lock(acceptMutex);
            // 새 StartAccept()를 차단하고, 진행 중인 accept의 완료 통지는 리스너가 비어 있으면 무시한다.
            bShutdown = true;
            acceptor.reset();
        }

        // Close()가 세션 맵에서 자신을 지우므로 맵 락 밖에서 호출한다.
        std::vector<SessionPtr> activeSessions;
        {
            std::lock_guard lock(sessionMutex);
            activeSessions.reserve(sessions.size());
            for (const auto& [id, spSession] : sessions)
                activeSessions.push_back(spSession);
        }
        for (const auto& spSession : activeSessions)
            spSession->Close();
        // 세션 종료 콜백에 기존 핸들러를 사용한 뒤 슬롯을 비운다.
        spHandler->store(nullptr);
    }

    IoContext* pIoContext;         // accept와 세션 I/O가 공유하는 컨텍스트
    NetServerConfig config;        // 리스닝 주소, backlog, 새 세션의 버퍼 설정
    std::shared_ptr<EventHandlerSlot> spHandler = std::make_shared<EventHandlerSlot>(nullptr); // 세션과 공유하는 이벤트 핸들러 슬롯
    std::recursive_mutex acceptMutex;       // 리스너 상태를 보호한다. accept 콜백에서 리스너를 중지할 때 재진입할 수 있다.
    bool bShutdown = false;                 // Shutdown 이후 새 리스닝을 차단한다.
    std::shared_ptr<std::function<void()>> spShutdown; // State가 종료 작업을 보유하고 IoContext는 약한 참조만 둔다.
    std::unique_ptr<Tcp::acceptor> acceptor; // 현재 리스너. 비어 있으면 수락 중지 상태다.
    uint64 acceptGeneration = 0;           // 이전 리스너의 완료 통지를 구분하는 시작 세대
    int64 nextSessionId = 1;               // 다음에 수락할 연결에 부여할 세션 ID
    std::mutex sessionMutex;               // 세션 맵 등록·조회·제거를 보호
    std::unordered_map<int64, SessionPtr> sessions; // 세션 ID별 현재 연결
};

NetServer::NetServer(IoContext* pIoContext)
    : m_state(std::make_shared<State>(pIoContext))
{
}

NetServer::~NetServer()
{
    Shutdown();
}

bool NetServer::Initialize(const NetServerConfig& config)
{
    auto& state = *m_state;
    std::lock_guard lock(state.acceptMutex);
    if (!state.pIoContext || !state.pIoContext->IsRunning() ||
        state.bShutdown || state.acceptor || config.recvBufSize <= static_cast<int32>(sizeof(PacketHeader)))
        return false;

    if (config.recvPacketReserveSize < 0)
        return false;

    // 종료 목록에 등록하면 IoContext는 acceptMutex를 얻어 리스너를 닫을 때까지 내부 상태를 유지한다.
    auto spShutdown = state.spShutdown;
    if (!spShutdown)
    {
        spShutdown = std::make_shared<std::function<void()>>([weak = state.weak_from_this()]()
        {
            if (auto self = weak.lock())
                self->shutdown();
        });
        if (!state.pIoContext->registerEndpoint(spShutdown))
            return false;
    }
    const int32 maxReserveSize = GetMaxPacketSize() - static_cast<int32>(sizeof(PacketHeader));
    if (config.recvPacketReserveSize > maxReserveSize)
        return false;
    state.spShutdown = std::move(spShutdown);
    state.config = config;
    return true;
}

bool NetServer::StartAccept()
{
    auto& state = *m_state;
    std::lock_guard lock(state.acceptMutex);
    if (state.bShutdown || state.acceptor || !state.spShutdown || !state.pIoContext->IsRunning())
        return false;

    ErrorCode error;
    const auto address = state.config.ip.empty()
        ? asio::ip::address_v4::any()
        : asio::ip::make_address_v4(state.config.ip, error);
    if (error)
        return false;

    auto acceptor = std::make_unique<Tcp::acceptor>(state.pIoContext->getContext());
    acceptor->open(Tcp::v4(), error);
    if (!error)
        acceptor->bind(Tcp::endpoint(address, state.config.port), error);
    if (!error)
        acceptor->listen(state.config.backlog, error);
    if (error)
        return false;

    state.acceptor = std::move(acceptor);
    ++state.acceptGeneration;
    state.postAccept();
    return true;
}

void NetServer::StopAccept()
{
    std::lock_guard lock(m_state->acceptMutex);
    m_state->acceptor.reset();
}

void NetServer::Shutdown()
{
    auto spState = m_state;
    spState->shutdown();
}

void NetServer::SetEventHandler(INetEventHandler* handler)
{
    std::lock_guard lock(m_state->acceptMutex);
    if (!m_state->bShutdown)
        m_state->spHandler->store(handler);
}

std::shared_ptr<ISession> NetServer::FindSession(int64 sessionId)
{
    std::lock_guard lock(m_state->sessionMutex);
    const auto iter = m_state->sessions.find(sessionId);
    return iter != m_state->sessions.end() ? iter->second : nullptr;
}

void NetServer::RemoveSession(int64 sessionId)
{
    SessionPtr spSession;
    {
        std::lock_guard lock(m_state->sessionMutex);
        const auto iter = m_state->sessions.find(sessionId);
        if (iter != m_state->sessions.end())
            spSession = iter->second;
    }
    if (spSession)
        spSession->Close();
}

int32 NetServer::GetSessionCount()
{
    std::lock_guard lock(m_state->sessionMutex);
    return static_cast<int32>(m_state->sessions.size());
}

INetEventHandler* NetServer::GetEventHandler()
{
    return m_state->spHandler->load();
}

PacketPool& NetServer::GetPacketPool()
{
    return m_state->pIoContext->GetPacketPool();
}

int32 NetServer::GetMaxPacketSize() const
{
    return m_state->pIoContext->GetConfig().maxPacketSize;
}

void NetServer::OnSessionDisconnected(std::shared_ptr<ISession> spSession)
{
    if (spSession)
        RemoveSession(spSession->GetId());
}

void NetServer::ScheduleAt(std::chrono::steady_clock::time_point deliverAt, std::function<void()> fn)
{
    m_state->pIoContext->ScheduleAt(deliverAt, std::move(fn));
}

} // namespace netlib
