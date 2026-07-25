#include "pch.h"
#include "LoginServer.h"

#include "Generated/DataStructures/account.pb.h"
#include "ProtoJsonSerializer.h"

bool LoginServer::OnInitialize()
{
    if (IsMetricsEnabled())
    {
        auto& registry = GetMetricsRegistry();
        // 사용자/route/session container 크기는 OnMetricsCollect에서 갱신하는 Gauge다.
        // 로그인 결과는 요청 처리 시 Counter로 누적해 invalid input, credential 실패, DB 오류, Gateway 부족을 서로 구분한다.
        bool registered = true;
        registered &= registry.AddGauge(serverbase::GaugeMetric::Login_LoggedInUsers, "mmo_login_users", "Users tracked as logged in.");
        registered &= registry.AddGauge(serverbase::GaugeMetric::Login_PreviousGatewayCache, "mmo_login_prev_gateway_cache", "Cached previous Gateway routes.");
        registered &= registry.AddGauge(serverbase::GaugeMetric::Login_GatewayConnections, "mmo_login_gateway_connections", "Authenticated Gateway connections.");
        registered &= registry.AddGauge(serverbase::GaugeMetric::Login_KnownGateways, "mmo_login_known_gateways", "Gateways currently known from Registry.");
        registered &= registry.AddCounter(serverbase::CounterMetric::Login_RequestSuccess, "mmo_login_requests_total", "Login request results.", { { "result", "success" } });
        registered &= registry.AddCounter(serverbase::CounterMetric::Login_RequestInvalidInput, "mmo_login_requests_total", "Login request results.", { { "result", "invalid_input" } });
        registered &= registry.AddCounter(serverbase::CounterMetric::Login_RequestInvalidCredentials, "mmo_login_requests_total", "Login request results.", { { "result", "invalid_credentials" } });
        registered &= registry.AddCounter(serverbase::CounterMetric::Login_RequestAccountError, "mmo_login_requests_total", "Login request results.", { { "result", "account_error" } });
        registered &= registry.AddCounter(serverbase::CounterMetric::Login_RequestNoGateway, "mmo_login_requests_total", "Login request results.", { { "result", "no_gateway" } });
        registered &= registry.AddCounter(serverbase::CounterMetric::Login_DuplicateLogin, "mmo_login_duplicate_total", "Duplicate login requests detected.");
        if (!registered)
            return false;
    }

    // 클라이언트 패킷의 패킷핸들러 등록
    m_packetDispatcher.Register<GamePacket::LoginReq>(Common::GAME_PACKET_ID_LOGIN_REQ,
        [this](auto& spSession, auto& msg) { handleLoginReq(spSession, msg); });

    m_packetDispatcher.SetUnknownPacketHandler([this](const netlib::ISessionPtr& spSession, const netlib::PacketPtr& spPacket)
    {
        LOG_WRITE(LogLevel::Warn, std::format("unknown client packetId={}", spPacket->GetHeader()->type));
        spSession->Disconnect();
    });

    // 게이트웨이 패킷의 패킷핸들러 등록
    m_gatewayDispatcher.Register<ServerPacket::ServerHandshakeRes>(Common::SERVER_PACKET_ID_SERVER_HANDSHAKE_RES,
        [this](auto& spSession, auto& msg) { handleGatewayHandshakeRes(spSession, msg); });
    m_gatewayDispatcher.Register<ServerPacket::UserDisconnectNtf>(Common::SERVER_PACKET_ID_USER_DISCONNECT_NTF,
        [this](auto& spSession, auto& msg) { handleUserDisconnectNtf(spSession, msg); });

    m_gatewayDispatcher.SetUnknownPacketHandler([this](const netlib::ISessionPtr& spSession, const netlib::PacketPtr& spPacket)
    {
        LOG_WRITE(LogLevel::Warn, std::format("unknown gateway packetId={} sessionId={}", spPacket->GetHeader()->type, spSession->GetId()));
    });

    // 클라이언트 네트워크 이벤트 핸들러 등록
    m_listenEventHandler.onAccept = [this](const netlib::ISessionPtr& spSession) { return onClientAccept(spSession); };
    m_listenEventHandler.onRecv = [this](const netlib::ISessionPtr& spSession, const netlib::PacketPtr& spPacket)
    {
        m_packetDispatcher.Dispatch(spSession, spPacket);
    };
    m_listenEventHandler.onDisconnect = [this](const netlib::ISessionPtr& spSession) { onClientDisconnect(spSession); };

    // 게이트웨이 연결 이벤트 핸들러 등록
    m_gatewayEventHandler.onConnect = [this](const netlib::ISessionPtr& spSession) { onGatewayConnect(spSession); };
    m_gatewayEventHandler.onRecv = [this](const netlib::ISessionPtr& spSession, const netlib::PacketPtr& spPacket)
    {
        m_gatewayDispatcher.Dispatch(spSession, spPacket);
    };
    m_gatewayEventHandler.onDisconnect = [this](const netlib::ISessionPtr& spSession) { onGatewayDisconnect(spSession); };

    // prevGatewayMap TTL 정리 타이머 (1분마다)
    GetTimer().Register(60000, [this]()
    {
        cleanupExpiredPrevGateway();
    });

    LOG_WRITE(LogLevel::Info, "complete");
    return true;
}

void LoginServer::OnMetricsCollect()
{
    // 이전 Gateway cache는 재접속 routing을 위한 임시 상태이므로 계속 증가하면 TTL cleanup 이상을 의심할 수 있다.
    // known Gateway와 authenticated connection을 함께 보면 Registry 정보와 실제 연결 상태의 차이를 확인할 수 있다.
    auto& registry = GetMetricsRegistry();
    registry.Set(serverbase::GaugeMetric::Login_LoggedInUsers, static_cast<double>(m_safeLoginMap.Size()));
    registry.Set(serverbase::GaugeMetric::Login_PreviousGatewayCache, static_cast<double>(m_safePrevGatewayMap.Size()));
    registry.Set(serverbase::GaugeMetric::Login_GatewayConnections, static_cast<double>(m_safeGatewaySessions.Size()));
    registry.Set(serverbase::GaugeMetric::Login_KnownGateways, static_cast<double>(m_safeGatewayInfos.Size()));
}

// 레지스트리 서버에서 다른서버 정보를 받음
void LoginServer::OnServerInfoUpdated(const ServerInfo& info)
{
    if (info.serverType != ServerType::Gateway)
        return;

    // 게이트웨이 정보 캐시 갱신
    if (info.status == ServerStatus::Disconnected)
        m_safeGatewayInfos.Erase(info.serverId);
    else
        m_safeGatewayInfos.Insert(info.serverId, info);

    if (info.status == ServerStatus::Running)
    {
        connectToGateway(info.serverId, info.privateIp, info.internalPort);
    }
    else if (info.status == ServerStatus::Disconnected)
    {
        disconnectFromGateway(info.serverId);
    }
}

// ServerBase::RequestShutdown 호출한 다음 호출됨
void LoginServer::OnBeforeShutdown()
{
    LOG_WRITE(LogLevel::Info, "LoginServer::OnBeforeShutdown");
}

// 클라이언트 accept 함
bool LoginServer::onClientAccept(const netlib::ISessionPtr& /*spSession*/)
{
    if (IsShuttingDown())
        return false;

    return true;
}

// 클라이언트 연결끊김
void LoginServer::onClientDisconnect(const netlib::ISessionPtr& /*spSession*/)
{
    // 로그인 처리 중 끊긴 경우 별도 처리 없음
    // 로그인맵은 TTL로 자동 만료
}

// 게이트웨이 서버에 연결성공
void LoginServer::onGatewayConnect(const netlib::ISessionPtr& spSession)
{
    // 아직 핸드셰이크 전이므로 세션에 빈 메타 정보만 설정해둔다.
    // 메타정보는 handleGatewayHandshakeRes 에서 채운다.
    spSession->SetUserData(std::make_shared<InternalSessionMeta>());

    // 게이트웨이 서버에 Handshake Req 전송
    ServerPacket::ServerHandshakeReq req;
    req.set_server_type(static_cast<int32>(ServerType::Login));
    req.set_server_id(m_serverId);
    netlib::PacketPtr spPacket = SerializePacket(Common::SERVER_PACKET_ID_SERVER_HANDSHAKE_REQ, req);
    spSession->Send(spPacket);

    LOG_WRITE(LogLevel::Info, std::format("gateway connected. send handshake req. sessionId={}", spSession->GetId()));
}
// 게이트웨이서버 연결끊김
void LoginServer::onGatewayDisconnect(const netlib::ISessionPtr& spSession)
{
    InternalSessionMeta* pMeta = getInternalSessionMeta(spSession);
    if (!pMeta || !pMeta->handshakeDone)
    {
        LOG_WRITE(LogLevel::Warn, std::format("gateway disconnected before handshake. sessionId={}", spSession->GetId()));
        return;
    }

    int32 gatewayId = pMeta->peerServerId;
    m_safeGatewaySessions.Erase(gatewayId);

    LOG_WRITE(LogLevel::Warn, std::format("gateway disconnected. gatewayId={}", gatewayId));
}

// 로그인 요청 처리 (코루틴 함수)
db::DetachedCoTask LoginServer::handleLoginReq(netlib::ISessionPtr spSession, GamePacket::LoginReq req)
{
    const std::string loginId = req.login_id();
    const std::string password = req.password();

    if (loginId.empty() || password.empty())
    {
        GetMetricsRegistry().Inc(serverbase::CounterMetric::Login_RequestInvalidInput);
        sendLoginFailed(spSession, "Invalid input");
        co_return;
    }


    // DB 비동기 조회. 완료될 때까지 suspend, 이후 IOCP Worker에서 코루틴 resume
    db::DBResult result = co_await GetDB().ExecuteAsync(
        db::EDBType::Account, 0,
        "SELECT data FROM Accounts WHERE login_name = ? LIMIT 1",
        { loginId },
        GetCoroutineResumeExecutor()   // IOCP Worker 스레드에서 resume
    );

    if (!result.success || result.IsEmpty())
    {
        GetMetricsRegistry().Inc(serverbase::CounterMetric::Login_RequestInvalidCredentials);
        LOG_WRITE(LogLevel::Info, std::format("login failed - account not found. loginId={}", loginId));
        sendLoginFailed(spSession, "Invalid ID or password");
        co_return;
    }

    // 조회된 모든 캐릭터를 protobuf 메시지로 역직렬화하여 목록에 적재.
    DataStructures::Account account;
    const std::string dataJson = result.GetString(0, "data");
    if (!packet::ProtoJsonSerializer::FromJson(dataJson, account))
    {
        GetMetricsRegistry().Inc(serverbase::CounterMetric::Login_RequestAccountError);
        LOG_WRITE(LogLevel::Error, std::format("failed to parse account JSON. loginId={}", loginId));
        co_return;
    }


    int64 accountId = account.account_id();
    std::string passwordHash = account.login_password_hash();

    // TODO: 실제 해시 검증으로 교체 (bcrypt 등)
    if (password != account.login_password_hash())
    {
        GetMetricsRegistry().Inc(serverbase::CounterMetric::Login_RequestInvalidCredentials);
        LOG_WRITE(LogLevel::Info, std::format("login failed - wrong password. loginId={}", loginId));
        sendLoginFailed(spSession, "Invalid ID or password");
        co_return;
    }

    // 중복 로그인 처리
    auto existingEntry = findLoginEntry(accountId);
    if (existingEntry.has_value())
    {
        GetMetricsRegistry().Inc(serverbase::CounterMetric::Login_DuplicateLogin);
        LOG_WRITE(LogLevel::Info, std::format("duplicate login detected. accountId={} existing gatewayId={}", accountId, existingEntry->gatewayServerId));
        sendDuplicateLoginToGateway(existingEntry->gatewayServerId, accountId);
    }

    // 게이트웨이 선택 (로드밸런싱)
    auto gateway = selectGateway(accountId);
    if (!gateway.has_value())
    {
        GetMetricsRegistry().Inc(serverbase::CounterMetric::Login_RequestNoGateway);
        LOG_WRITE(LogLevel::Warn, std::format("no available gateway for accountId={}", accountId));
        sendLoginFailed(spSession, "Server is busy. Please try again later.");
        co_return;
    }

    // 인증 토큰 생성
    uint64 authToken = generateAuthToken();
    int64 expireTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() + k_authTokenTtlMs;

    sendAuthTokenToGateway(gateway->serverId, accountId, authToken, expireTimeMs);
    upsertLoginEntry(accountId, gateway->serverId);
    sendLoginSuccess(spSession, accountId, authToken, *gateway);
    GetMetricsRegistry().Inc(serverbase::CounterMetric::Login_RequestSuccess);

    LOG_WRITE(LogLevel::Info, std::format("login success. accountId={} gateway={}:{}", accountId, gateway->privateIp, gateway->internalPort));
}

// 게이트웨이서버로부터 HandshakeRes를 받음
void LoginServer::handleGatewayHandshakeRes(const netlib::ISessionPtr& spSession, const ServerPacket::ServerHandshakeRes& msg)
{
    int32 gatewayId = msg.server_id();

    if (!msg.success())
    {
        LOG_WRITE(LogLevel::Error, std::format("gateway handshake failed. gatewayId={} error='{}'", gatewayId, msg.error_msg()));
        return;
    }

    InternalSessionMeta* pMeta = getInternalSessionMeta(spSession);
    if (pMeta->handshakeDone)
    {
        LOG_WRITE(LogLevel::Error, std::format("GatewayServer의 HandshakeRes가 중복으로 들어옴. gatewayId={}", gatewayId));
        return;
    }
    
    pMeta->handshakeDone = true;
    pMeta->isConnector = true;
    pMeta->peerServerId = gatewayId;
    pMeta->peerServerType = ServerType::Gateway;
    
    m_safeGatewaySessions.Insert(gatewayId, spSession);

    LOG_WRITE(LogLevel::Info, std::format("gateway handshake complete. gatewayId={}, sessionId={}", gatewayId, spSession->GetId()));
}

// 로그인 성공 응답
void LoginServer::sendLoginSuccess(const netlib::ISessionPtr& spSession, int64 accountId, uint64 authToken, const ServerInfo& gatewayInfo)
{
    GamePacket::LoginRes res;
    res.set_success(true);
    res.set_account_id(accountId);
    res.set_auth_token(authToken);
    res.set_gateway_ip(gatewayInfo.publicIp);   // 클라이언트는 외부접속 주소(PublicIP)로 게이트웨이에 접속한다
    res.set_gateway_port(gatewayInfo.clientPort);

    auto spPacket = SerializePacket(Common::GAME_PACKET_ID_LOGIN_RES, res);
    if (spPacket)
        spSession->Send(spPacket);
}

// 로그인 실패 응답
void LoginServer::sendLoginFailed(const netlib::ISessionPtr& spSession, const std::string& errorMsg)
{
    GamePacket::LoginRes res;
    res.set_success(false);
    res.set_error_msg(errorMsg);

    auto spPacket = SerializePacket(Common::GAME_PACKET_ID_LOGIN_RES, res);
    if (spPacket)
        spSession->Send(spPacket);
}


// 게이트웨이서버에 연결
void LoginServer::connectToGateway(int32 gatewayId, const std::string& ip, uint16 port)
{
    if (m_safeGatewayClients.Contains(gatewayId))
        return;

    netlib::NetClientPtr spClient = ConnectToServer(ip, port, m_gatewayEventHandler);
    if (spClient)
    {
        m_safeGatewayClients.Insert(gatewayId, spClient);
        LOG_WRITE(LogLevel::Info, std::format("connecting to gateway {} {}:{}", gatewayId, ip, port));
    }
}

// 게이트웨이서버의 연결을 끊음
void LoginServer::disconnectFromGateway(int32 gatewayId)
{
    netlib::NetClientPtr spClient;
    if (!m_safeGatewayClients.EraseAndGet(gatewayId, spClient))
        return;

    if (spClient)
        DisconnectToServer(spClient);

    // m_safeGatewaySessions 정리는 onGatewayDisconnect 콜백에서 자동 처리됨

    LOG_WRITE(LogLevel::Info, std::format("disconnected from gateway {}", gatewayId));
}

std::optional<ServerInfo> LoginServer::selectGateway(int64 accountId) const
{
    if (m_safeGatewayInfos.Empty())
        return std::nullopt;

    // 이전 접속 게이트웨이 우선 선택 (5분 TTL)
    PrevGatewayEntry prevEntry;
    if (m_safePrevGatewayMap.Find(accountId, prevEntry))
    {
        if (std::chrono::steady_clock::now() < prevEntry.expireTime)
        {
            ServerInfo prevInfo;
            if (m_safeGatewayInfos.Find(prevEntry.gatewayServerId, prevInfo))
            {
                if (prevInfo.status == ServerStatus::Running)
                {
                    return prevInfo;
                }
            }
        }
    }

    // 유저 수가 가장 적은 Running 게이트웨이 선택
    std::optional<ServerInfo> best;
    m_safeGatewayInfos.ForEach([&](const int32&, const ServerInfo& info)
    {
        if (info.status != ServerStatus::Running)
            return;

        if (!best.has_value() || info.userCount < best->userCount)
            best = info;
    });

    return best;
}

void LoginServer::sendAuthTokenToGateway(int32 gatewayId, int64 accountId, uint64 authToken, int64 expireTimeMs)
{
    netlib::ISessionPtr spSession;
    if (!m_safeGatewaySessions.Find(gatewayId, spSession))
    {
        LOG_WRITE(LogLevel::Warn, std::format("no session for gatewayId={}", gatewayId));
        return;
    }

    ServerPacket::LoginAuthTokenNtf ntf;
    ntf.set_account_id(accountId);
    ntf.set_auth_token(authToken);
    ntf.set_expire_time_ms(expireTimeMs);

    auto spPacket = SerializePacket(Common::SERVER_PACKET_ID_LOGIN_AUTH_TOKEN_NTF, ntf);
    if (spPacket)
        spSession->Send(spPacket);
}

void LoginServer::sendDuplicateLoginToGateway(int32 gatewayId, int64 accountId)
{
    netlib::ISessionPtr spSession;
    if (!m_safeGatewaySessions.Find(gatewayId, spSession))
        return;

    ServerPacket::LoginDuplicateNtf ntf;
    ntf.set_account_id(accountId);

    auto spPacket = SerializePacket(Common::SERVER_PACKET_ID_LOGIN_DUPLICATE_NTF, ntf);
    if (spPacket)
        spSession->Send(spPacket);
}


// 로그인맵 업데이트
void LoginServer::upsertLoginEntry(int64 accountId, int32 gatewayServerId)
{
    auto now = std::chrono::steady_clock::now();

    m_safeLoginMap.Insert(accountId, { accountId, gatewayServerId });
    m_safePrevGatewayMap.Insert(accountId, { gatewayServerId, now + std::chrono::milliseconds(k_prevGatewayTtlMs) });
}

void LoginServer::removeLoginEntry(int64 accountId)
{
    m_safeLoginMap.Erase(accountId);
}

std::optional<LoginServer::LoginEntry> LoginServer::findLoginEntry(int64 accountId) const
{
    LoginEntry entry;
    if (!m_safeLoginMap.Find(accountId, entry))
        return std::nullopt;

    return entry;
}

void LoginServer::cleanupExpiredPrevGateway()
{
    auto now = std::chrono::steady_clock::now();

    std::vector<int64> expiredKeys = m_safePrevGatewayMap.CollectKeys(
        [now](const int64&, const PrevGatewayEntry& e) { return now >= e.expireTime; });

    for (int64 key : expiredKeys)
        m_safePrevGatewayMap.Erase(key);
}

// 게이트웨이서버에서 유저 접속끊김 노티를 받음
void LoginServer::handleUserDisconnectNtf(const netlib::ISessionPtr& /*spSession*/, const ServerPacket::UserDisconnectNtf& ntf)
{
    int64 accountId = ntf.account_id();
    removeLoginEntry(accountId);
    LOG_WRITE(LogLevel::Info, std::format("user disconnected, removed from loginMap. accountId={}", accountId));
}

// 인증 토큰 생성
uint64 LoginServer::generateAuthToken()
{
    std::lock_guard<std::mutex> lock(m_rngMutex);
    return m_rng();
}

// 세션에서 InternalSessionMeta를 꺼낸다.
// 주의: InternalSessionMeta* 를 다른곳에 보관해두면 안됨. 세션이 제거될때 함께 제거되기 때문
InternalSessionMeta* LoginServer::getInternalSessionMeta(const netlib::ISessionPtr& spSession)
{
    return static_cast<InternalSessionMeta*>(spSession->GetUserData().get());
}
