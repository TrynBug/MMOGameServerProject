#include "NetClient.h"
#include "IoContext.h"
#include "Session.h"
#include "../NetLibCommon/NetLibStats.h"

#include <algorithm>
#include <unordered_map>

namespace netlib
{

struct NetClient::State : std::enable_shared_from_this<State>
{
    explicit State(IoContext* pIoContext) : pIoContext(pIoContext) {}

    bool postConnect()
    {
        // State 락을 잡고 호출한다. 컨텍스트 종료가 시작됐으면 새 소켓을 만들지 않는다.
        if (bShutdown || !pIoContext->IsRunning())
            return false;

        connectingSocket = std::make_unique<Tcp::socket>(pIoContext->getContext());
        NetLibStats::Inc(StatCounter::ConnectPosted);
        connectingSocket->async_connect(remote, [self = shared_from_this(), generation = generation](const ErrorCode& error)
        {
            std::lock_guard lock(self->mutex);
            if (generation != self->generation || !self->pIoContext->IsRunning())
                return;

            NetLibStats::Inc(StatCounter::ConnectCompleted);
            // 성공하면 접속용 소켓의 소유권을 Session으로 넘긴다.
            auto socket = std::move(self->connectingSocket);
            if (error)
            {
                NetLibStats::Inc(StatCounter::ConnectFailed);
                if (auto* handler = self->spHandler->load())
                    handler->OnLog(LogLevel::Info, nullptr, "Connect failed: " + error.message());
                if (generation == self->generation)
                    self->scheduleReconnect();
                return;
            }
            self->startSession(std::move(*socket));
        });
        return true;
    }

    void startSession(Tcp::socket socket)
    {
        Session::SetSocketOptions(socket, config.bUseNagle);
        auto spSession = std::make_shared<Session>(
            pIoContext->getContext(), pIoContext->GetPacketPool(), pIoContext->GetConfig().maxPacketSize,
            nextSessionId++, std::move(socket), remote, config.recvBufSize, 0,
            spHandler, [weak = weak_from_this(), generation = generation](int64 id)
            {
                if (auto self = weak.lock())
                {
                    std::lock_guard lock(self->mutex);
                    // Session::Close()가 사용자 OnDisconnect를 호출한 뒤에 이 세션을 제거한다.
                    self->sessions.erase(id);
                    // 아직 현재 연결인 세션이 종료된 경우에만 재접속을 예약한다.
                    if (generation == self->generation && self->session && self->session->GetId() == id)
                    {
                        self->session.reset();
                        self->scheduleReconnect();
                    }
                }
            });
        sessions.emplace(spSession->GetId(), spSession);
        session = spSession;
        spSession->Start(false);
    }

    // 접속 완료 또는 세션 종료 경로에서 State의 mutex를 잡은 상태로 호출한다.
    void scheduleReconnect()
    {
        if (bShutdown || !config.bAutoReconnect || !pIoContext->IsRunning())
            return;

        // 재접속 대기를 타이머에 맡겨 워커가 그동안 다른 I/O를 처리하게 한다.
        reconnectTimer = std::make_unique<asio::steady_timer>(pIoContext->getContext());
        reconnectTimer->expires_after(std::chrono::milliseconds(std::max(1, config.reconnectIntervalMs)));
        reconnectTimer->async_wait([self = shared_from_this(), generation = generation](const ErrorCode& error)
        {
            std::lock_guard lock(self->mutex);
            if (!error && generation == self->generation)
            {
                self->reconnectTimer.reset();
                self->postConnect();
            }
        });
    }

    void shutdown()
    {
        std::vector<SessionPtr> activeSessions;
        {
            std::lock_guard lock(mutex);
            bShutdown = true;
            cancelConnect();
            session.reset();
            // Connect/Disconnect가 교체 중인 세션도 OnDisconnect가 끝날 때까지 추적한다.
            activeSessions.reserve(sessions.size());
            for (const auto& [id, spSession] : sessions)
                activeSessions.push_back(spSession);
        }
        // Close()가 세션 락을 잡고 정리 콜백에서 State 락을 다시 잡으므로 여기서는 State 락을 푼다.
        for (const auto& spSession : activeSessions)
            spSession->Close();
        // OnDisconnect가 기존 핸들러에 전달된 뒤 슬롯을 비운다.
        spHandler->store(nullptr);
    }

    // 이전 연결 시도와 재접속 예약의 완료 핸들러를 무효화한다.
    void cancelConnect()
    {
        ++generation;
        reconnectTimer.reset();
        connectingSocket.reset();
    }

    IoContext* pIoContext;      // 접속, 세션 I/O, 재접속 타이머가 공유하는 컨텍스트
    NetClientConfig config;     // 수신 버퍼 크기, 소켓 옵션, 자동 재접속 설정
    std::shared_ptr<EventHandlerSlot> spHandler = std::make_shared<EventHandlerSlot>(nullptr); // 세션과 공유하는 이벤트 핸들러 슬롯
    std::recursive_mutex mutex; // 접속·재접속 상태를 보호하고 이벤트 콜백에서의 재진입을 허용
    bool bShutdown = false;     // Shutdown 이후 새 접속을 차단한다.
    std::shared_ptr<std::function<void()>> spShutdown; // State가 종료 작업을 보유하고 IoContext는 약한 참조만 둔다.
    SessionPtr session;        // 연결에 성공한 현재 세션
    std::unordered_map<int64, SessionPtr> sessions; // 현재 연결과 아직 종료 콜백이 남은 이전 연결
    Tcp::endpoint remote;      // 최초 접속과 자동 재접속에 사용할 원격 주소
    std::unique_ptr<Tcp::socket> connectingSocket; // 연결이 완료되면 Session으로 넘기는 접속용 소켓
    std::unique_ptr<asio::steady_timer> reconnectTimer; // 다음 자동 재접속 시도를 기다리는 타이머
    uint64 generation = 0;    // 새 Connect() 또는 Disconnect()가 이전 완료 통지를 무효화하는 데 사용
    int64 nextSessionId = 1;   // 연결에 성공할 때 부여할 다음 세션 ID
};

NetClient::NetClient(IoContext* pIoContext)
    : m_state(std::make_shared<State>(pIoContext))
{
}

NetClient::~NetClient()
{
    Shutdown();
}

bool NetClient::Initialize(const NetClientConfig& config)
{
    auto& state = *m_state;
    std::lock_guard lock(state.mutex);
    if (!state.pIoContext || !state.pIoContext->IsRunning() ||
        state.bShutdown || config.recvBufSize <= static_cast<int32>(sizeof(PacketHeader)))
        return false;

    if (!state.spShutdown)
    {
        auto spShutdown = std::make_shared<std::function<void()>>([weak = state.weak_from_this()]()
        {
            if (auto self = weak.lock())
                self->shutdown();
        });
        if (!state.pIoContext->registerEndpoint(spShutdown))
            return false;
        state.spShutdown = std::move(spShutdown);
    }
    state.config = config;
    return true;
}

void NetClient::Shutdown()
{
    auto spState = m_state;
    spState->shutdown();
}

void NetClient::SetEventHandler(INetEventHandler* handler)
{
    std::lock_guard lock(m_state->mutex);
    if (!m_state->bShutdown)
        m_state->spHandler->store(handler);
}

bool NetClient::Connect(const std::string& ip, uint16 port)
{
    auto spState = m_state;
    ErrorCode error;
    const auto address = asio::ip::make_address_v4(ip, error);
    if (error)
        return false;

    SessionPtr spOldSession;
    uint64 generation;
    {
        std::lock_guard lock(spState->mutex);
        if (spState->bShutdown || !spState->spShutdown || !spState->pIoContext->IsRunning())
            return false;
        spState->cancelConnect();
        generation = spState->generation;
        spOldSession = std::move(spState->session);
        spState->remote = Tcp::endpoint(address, port);
    }
    // Close()가 OnDisconnect와 세션 정리 콜백을 부르므로 State 락 밖에서 실행한다.
    if (spOldSession)
        spOldSession->Close();

    // 기존 세션을 닫는 사이 Shutdown()이나 다른 Connect()가 세대를 변경했을 수 있다.
    std::lock_guard lock(spState->mutex);
    return generation == spState->generation && spState->postConnect();
}

void NetClient::Disconnect()
{
    auto spState = m_state;
    SessionPtr spSession;
    {
        std::lock_guard lock(spState->mutex);
        // 세대를 먼저 변경해 현재 세션을 닫을 때 자동 재접속 예약을 막는다.
        spState->cancelConnect();
        spSession = std::move(spState->session);
    }
    if (spSession)
        spSession->Close();
}

std::shared_ptr<ISession> NetClient::GetSession()
{
    std::lock_guard lock(m_state->mutex);
    return m_state->session;
}

bool NetClient::IsConnected()
{
    auto spSession = GetSession();
    return spSession && spSession->IsConnected();
}

INetEventHandler* NetClient::GetEventHandler()
{
    return m_state->spHandler->load();
}

PacketPool& NetClient::GetPacketPool()
{
    return m_state->pIoContext->GetPacketPool();
}

int32 NetClient::GetMaxPacketSize() const
{
    return m_state->pIoContext->GetConfig().maxPacketSize;
}

void NetClient::OnSessionDisconnected(std::shared_ptr<ISession> spSession)
{
    if (spSession)
        spSession->Disconnect();
}

void NetClient::ScheduleAt(std::chrono::steady_clock::time_point deliverAt, std::function<void()> fn)
{
    m_state->pIoContext->ScheduleAt(deliverAt, std::move(fn));
}

} // namespace netlib
