#include "pch.h"
#include "LoginServer.h"

bool LoginServer::OnInitialize()
{
    // 클라이언트 패킷의 패킷핸들러 등록
    m_packetDispatcher.Register<GamePacket::LoginReq>(Common::GAME_PACKET_ID_LOGIN_REQ,
        [this](auto& spSession, auto& msg) { handleLoginReq(spSession, msg); });

    m_packetDispatcher.SetUnknownPacketHandler([this](const netlib::ISessionPtr& spSession, const netlib::PacketPtr& spPacket)
    {
        LOG_WRITE(LogLevel::Warn, std::format("LoginServer: unknown client packetId={}", spPacket->GetHeader()->type));
        spSession->Disconnect();
    });

    // 게이트웨이 패킷의 패킷핸들러 등록
    m_gatewayDispatcher.Register<ServerPacket::GatewayHandshakeNtf>(Common::SERVER_PACKET_ID_LOGIN_GATEWAY_HANDSHAKE_NTF,
        [this](auto& spSession, auto& msg) { handleGatewayHandshake(spSession, msg); });

    m_gatewayDispatcher.Register<ServerPacket::UserDisconnectNtf>(Common::SERVER_PACKET_ID_USER_DISCONNECT_NTF,
        [this](auto& spSession, auto& msg) { handleUserDisconnectNtf(spSession, msg); });

    m_gatewayDispatcher.SetUnknownPacketHandler([this](const netlib::ISessionPtr& spSession, const netlib::PacketPtr& spPacket)
    {
        LOG_WRITE(LogLevel::Warn, std::format("LoginServer: unknown gateway packetId={} sessionId={}", spPacket->GetHeader()->type, spSession->GetId()));
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

    // AccountDB 초기화
    // TODO: 경로를 설정 파일에서 읽도록 개선
    if (!m_dbQueue.Open("AccountDB.db", 1))
    {
        LOG_WRITE(LogLevel::Error, "LoginServer::OnInitialize - failed to open AccountDB");
        return false;
    }

    // AccountDB 스키마 초기화 (테이블이 없으면 생성)
    initAccountDB();

    // prevGatewayMap TTL 정리 타이머 (1분마다)
    GetTimer().Register(60000, [this]()
    {
        cleanupExpiredPrevGateway();
    });

    LOG_WRITE(LogLevel::Info, "LoginServer::OnInitialize complete");
    return true;
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
        connectToGateway(info.serverId, info.ip, info.internalPort);
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
    m_dbQueue.Close();
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
    // gatewayServerId는 GatewayHandshakeNtf 수신 후 채운다.
    spSession->SetUserData(std::make_shared<GatewaySessionMetaInfo>());
    LOG_WRITE(LogLevel::Info, std::format("LoginServer: gateway connected. sessionId={}", spSession->GetId()));
}

// 게이트웨이서버 연결끊김
void LoginServer::onGatewayDisconnect(const netlib::ISessionPtr& spSession)
{
    GatewaySessionMetaInfo* pMeta = getGatewaySessionMeta(spSession);
    if (!pMeta || pMeta->gatewayServerId == 0)
    {
        LOG_WRITE(LogLevel::Warn, std::format("LoginServer: gateway disconnected before handshake. sessionId={}", spSession->GetId()));
        return;
    }

    int32 gatewayId = pMeta->gatewayServerId;
    m_safeGatewaySessions.Erase(gatewayId);

    LOG_WRITE(LogLevel::Warn, std::format("LoginServer: gateway disconnected. gatewayId={}", gatewayId));
}

// 로그인 요청 처리 (코루틴 함수)
db::DBTask<void> LoginServer::handleLoginReq(netlib::ISessionPtr spSession, GamePacket::LoginReq req)
{
    const std::string loginId = req.login_id();
    const std::string password = req.password();

    if (loginId.empty() || password.empty())
    {
        sendLoginFailed(spSession, "Invalid input");
        co_return;
    }

    // DB 비동기 조회. 완료될 때까지 suspend, 이후 IOCP Worker에서 코루틴 resume
    db::DBResult result = co_await m_dbQueue.ExecuteAsync(
        "SELECT user_id, password_hash FROM accounts WHERE login_id = ? LIMIT 1",
        { loginId },
        GetCoroutineResumeExecutor()   // IOCP Worker 스레드에서 resume
    );

    if (!result.success || result.IsEmpty())
    {
        LOG_WRITE(LogLevel::Info, std::format("LoginServer: login failed - user not found. loginId={}", loginId));
        sendLoginFailed(spSession, "Invalid ID or password");
        co_return;
    }

    int64 userId = result.GetInt64(0, "user_id");
    std::string passwordHash = result.GetString(0, "password_hash");

    // TODO: 실제 해시 검증으로 교체 (bcrypt 등)
    if (passwordHash != password)
    {
        LOG_WRITE(LogLevel::Info, std::format("LoginServer: login failed - wrong password. loginId={}", loginId));
        sendLoginFailed(spSession, "Invalid ID or password");
        co_return;
    }

    // 중복 로그인 처리
    auto existingEntry = findLoginEntry(userId);
    if (existingEntry.has_value())
    {
        LOG_WRITE(LogLevel::Info, std::format("LoginServer: duplicate login detected. userId={} existing gatewayId={}", userId, existingEntry->gatewayServerId));
        sendDuplicateLoginToGateway(existingEntry->gatewayServerId, userId);
    }

    // 게이트웨이 선택 (로드밸런싱)
    auto gateway = selectGateway(userId);
    if (!gateway.has_value())
    {
        LOG_WRITE(LogLevel::Warn, std::format("LoginServer: no available gateway for userId={}", userId));
        sendLoginFailed(spSession, "Server is busy. Please try again later.");
        co_return;
    }

    // 인증 토큰 생성
    uint64 authToken = generateAuthToken();
    int64 expireTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() + k_authTokenTtlMs;

    sendAuthTokenToGateway(gateway->serverId, userId, authToken, expireTimeMs);
    upsertLoginEntry(userId, gateway->serverId);
    sendLoginSuccess(spSession, userId, authToken, *gateway);

    LOG_WRITE(LogLevel::Info, std::format("LoginServer: login success. userId={} gateway={}:{}", userId, gateway->ip, gateway->internalPort));
}

// 게이트웨이서버 접속 성공후 게이트웨이서버가 자신의 정보를 보냄
void LoginServer::handleGatewayHandshake(const netlib::ISessionPtr& spSession, const ServerPacket::GatewayHandshakeNtf& ntf)
{
    int32 gatewayId = ntf.server_id();

    m_safeGatewaySessions.Insert(gatewayId, spSession);

    GatewaySessionMetaInfo* pMeta = getGatewaySessionMeta(spSession);
    if (pMeta)
        pMeta->gatewayServerId = gatewayId;

    LOG_WRITE(LogLevel::Info, std::format("LoginServer: gateway handshake complete. gatewayId={} sessionId={}", gatewayId, spSession->GetId()));
}

// 로그인 성공 응답
void LoginServer::sendLoginSuccess(const netlib::ISessionPtr& spSession, int64 userId, uint64 authToken, const ServerInfo& gatewayInfo)
{
    GamePacket::LoginRes res;
    res.set_success(true);
    res.set_user_id(userId);
    res.set_auth_token(authToken);
    res.set_gateway_ip(gatewayInfo.ip);
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
        LOG_WRITE(LogLevel::Info, std::format("LoginServer: connecting to gateway {} {}:{}", gatewayId, ip, port));
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

    LOG_WRITE(LogLevel::Info, std::format("LoginServer: disconnected from gateway {}", gatewayId));
}

std::optional<ServerInfo> LoginServer::selectGateway(int64 userId) const
{
    if (m_safeGatewayInfos.Empty())
        return std::nullopt;

    // 이전 접속 게이트웨이 우선 선택 (5분 TTL)
    PrevGatewayEntry prevEntry;
    if (m_safePrevGatewayMap.Find(userId, prevEntry))
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

void LoginServer::sendAuthTokenToGateway(int32 gatewayId, int64 userId, uint64 authToken, int64 expireTimeMs)
{
    netlib::ISessionPtr spSession;
    if (!m_safeGatewaySessions.Find(gatewayId, spSession))
    {
        LOG_WRITE(LogLevel::Warn, std::format("LoginServer::sendAuthTokenToGateway - no session for gatewayId={}", gatewayId));
        return;
    }

    ServerPacket::LoginAuthTokenNtf ntf;
    ntf.set_user_id(userId);
    ntf.set_auth_token(authToken);
    ntf.set_expire_time_ms(expireTimeMs);

    auto spPacket = SerializePacket(Common::SERVER_PACKET_ID_LOGIN_AUTH_TOKEN_NTF, ntf);
    if (spPacket)
        spSession->Send(spPacket);
}

void LoginServer::sendDuplicateLoginToGateway(int32 gatewayId, int64 userId)
{
    netlib::ISessionPtr spSession;
    if (!m_safeGatewaySessions.Find(gatewayId, spSession))
        return;

    ServerPacket::LoginDuplicateNtf ntf;
    ntf.set_user_id(userId);

    auto spPacket = SerializePacket(Common::SERVER_PACKET_ID_LOGIN_DUPLICATE_NTF, ntf);
    if (spPacket)
        spSession->Send(spPacket);
}


// 로그인맵 업데이트
void LoginServer::upsertLoginEntry(int64 userId, int32 gatewayServerId)
{
    auto now = std::chrono::steady_clock::now();

    m_safeLoginMap.Insert(userId, { userId, gatewayServerId });
    m_safePrevGatewayMap.Insert(userId, { gatewayServerId, now + std::chrono::milliseconds(k_prevGatewayTtlMs) });
}

void LoginServer::removeLoginEntry(int64 userId)
{
    m_safeLoginMap.Erase(userId);
}

std::optional<LoginServer::LoginEntry> LoginServer::findLoginEntry(int64 userId) const
{
    LoginEntry entry;
    if (!m_safeLoginMap.Find(userId, entry))
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
    int64 userId = ntf.user_id();
    removeLoginEntry(userId);
    LOG_WRITE(LogLevel::Info, std::format("LoginServer: user disconnected, removed from loginMap. userId={}", userId));
}

// 인증 토큰 생성
uint64 LoginServer::generateAuthToken()
{
    std::lock_guard<std::mutex> lock(m_rngMutex);
    return m_rng();
}

// 세션에서 GatewaySessionMetaInfo를 꺼낸다.
// 주의: GatewaySessionMetaInfo* 를 다른곳에 보관해두면 안됨. 세션이 제거될때 함께 제거되기 때문
GatewaySessionMetaInfo* LoginServer::getGatewaySessionMeta(const netlib::ISessionPtr& spSession)
{
    return static_cast<GatewaySessionMetaInfo*>(spSession->GetUserData().get());
}

// AccountDB 스키마 초기화
void LoginServer::initAccountDB()
{
    // 동기 DBConnection으로 스키마 초기화 (서버 시작 시 1회)
    db::DBConnection conn;
    if (!conn.Open("AccountDB.db"))
    {
        LOG_WRITE(LogLevel::Error, "LoginServer::initAccountDB - failed to open DB");
        return;
    }

    conn.Execute(R"(
        CREATE TABLE IF NOT EXISTS accounts (
            user_id       INTEGER PRIMARY KEY AUTOINCREMENT,
            login_id      TEXT    NOT NULL UNIQUE,
            password_hash TEXT    NOT NULL,
            created_at    INTEGER NOT NULL DEFAULT (strftime('%s','now') * 1000)
        )
    )");

    LOG_WRITE(LogLevel::Info, "LoginServer::initAccountDB - schema ready");
}
