#include "pch.h"
#include "LoginServer.h"

bool LoginServer::OnInitialize()
{
    // 클라이언트 패킷의 패킷핸들러 등록
    m_packetDispatcher.Register<GamePacket::LoginReq>(Common::GAME_PACKET_ID_LOGIN_REQ,
        [this](auto& spSession, auto& msg) { handleLoginReq(spSession, msg); });

    m_packetDispatcher.SetUnknownPacketHandler([this](const netlib::ISessionPtr& spSession, const netlib::PacketPtr& spPacket)
    {
        LOG_WRITE(LogLevel::Warn, "LoginServer: unknown client packetId=" + std::to_string(spPacket->GetHeader()->type));
        spSession->Disconnect();
    });

    // 게이트웨이 패킷의 패킷핸들러 등록
    m_gatewayDispatcher.Register<ServerPacket::GatewayHandshakeNtf>(Common::SERVER_PACKET_ID_LOGIN_GATEWAY_HANDSHAKE_NTF,
        [this](auto& spSession, auto& msg) { handleGatewayHandshake(spSession, msg); });

    m_gatewayDispatcher.Register<ServerPacket::UserDisconnectNtf>(Common::SERVER_PACKET_ID_USER_DISCONNECT_NTF,
        [this](auto& spSession, auto& msg) { handleUserDisconnectNtf(spSession, msg); });

    m_gatewayDispatcher.SetUnknownPacketHandler([this](const netlib::ISessionPtr& spSession, const netlib::PacketPtr& spPacket)
    {
        LOG_WRITE(LogLevel::Warn, "LoginServer: unknown gateway packetId=" + std::to_string(spPacket->GetHeader()->type) + " sessionId=" + std::to_string(spSession->GetId()));
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
    {
        std::unique_lock<std::shared_mutex> lock(m_gatewayInfosMutex);
        if (info.status == ServerStatus::Disconnected)
            m_gatewayInfos.erase(info.serverId);
        else
            m_gatewayInfos[info.serverId] = info;
    }

    if (info.status == ServerStatus::Running)
    {
        connectToGateway(info.serverId, info.ip, info.port);
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
    // 아직 핸드셰이크 전이므로 세션만 기록해둔다.
    // m_gatewaySessions 등록은 GatewayHandshakeNtf 수신 후 처리한다.
    LOG_WRITE(LogLevel::Info, "LoginServer: gateway connected. sessionId=" + std::to_string(spSession->GetId()));
}

// 게이트웨이서버 연결끊김
void LoginServer::onGatewayDisconnect(const netlib::ISessionPtr& spSession)
{
    int64 sessionId = spSession->GetId();

    // sessionId -> gatewayServerId 조회 후 정리
    int32 gatewayId = 0;
    {
        std::unique_lock<std::shared_mutex> lock(m_sessionToGatewayMutex);
        auto iter = m_sessionToGatewayId.find(sessionId);
        if (iter == m_sessionToGatewayId.end())
        {
            // 핸드셰이크 전에 끊긴 경우
            LOG_WRITE(LogLevel::Warn, "LoginServer: gateway disconnected before handshake. sessionId=" + std::to_string(sessionId));
            return;
        }

        gatewayId = iter->second;
        m_sessionToGatewayId.erase(iter);
    }

    {
        std::unique_lock<std::shared_mutex> lock(m_gatewaySessionsMutex);
        m_gatewaySessions.erase(gatewayId);
    }

    LOG_WRITE(LogLevel::Warn, "LoginServer: gateway disconnected. gatewayId=" + std::to_string(gatewayId));
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
        LOG_WRITE(LogLevel::Info, "LoginServer: login failed - user not found. loginId=" + loginId);
        sendLoginFailed(spSession, "Invalid ID or password");
        co_return;
    }

    int64 userId = result.GetInt64(0, "user_id");
    std::string passwordHash = result.GetString(0, "password_hash");

    // TODO: 실제 해시 검증으로 교체 (bcrypt 등)
    if (passwordHash != password)
    {
        LOG_WRITE(LogLevel::Info, "LoginServer: login failed - wrong password. loginId=" + loginId);
        sendLoginFailed(spSession, "Invalid ID or password");
        co_return;
    }

    // 중복 로그인 처리
    auto existingEntry = findLoginEntry(userId);
    if (existingEntry.has_value())
    {
        LOG_WRITE(LogLevel::Info, "LoginServer: duplicate login detected. userId=" + std::to_string(userId) + " existing gatewayId=" + std::to_string(existingEntry->gatewayServerId));
        sendDuplicateLoginToGateway(existingEntry->gatewayServerId, userId);
    }

    // 게이트웨이 선택 (로드밸런싱)
    auto gateway = selectGateway(userId);
    if (!gateway.has_value())
    {
        LOG_WRITE(LogLevel::Warn, "LoginServer: no available gateway for userId=" + std::to_string(userId));
        sendLoginFailed(spSession, "Server is busy. Please try again later.");
        co_return;
    }

    // 인증 토큰 생성
    uint64 authToken = generateAuthToken();
    int64 expireTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() + k_authTokenTtlMs;

    sendAuthTokenToGateway(gateway->serverId, userId, authToken, expireTimeMs);
    upsertLoginEntry(userId, gateway->serverId);
    sendLoginSuccess(spSession, userId, authToken, *gateway);

    LOG_WRITE(LogLevel::Info, "LoginServer: login success. userId=" + std::to_string(userId) + " gateway=" + gateway->ip + ":" + std::to_string(gateway->port));
}

// 게이트웨이서버 접속 성공후 게이트웨이서버가 자신의 정보를 보냄
void LoginServer::handleGatewayHandshake(const netlib::ISessionPtr& spSession, const ServerPacket::GatewayHandshakeNtf& ntf)
{
    int32 gatewayId = ntf.server_id();
    int64 sessionId = spSession->GetId();

    {
        std::unique_lock<std::shared_mutex> lock(m_gatewaySessionsMutex);
        m_gatewaySessions[gatewayId] = spSession;
    }

    {
        std::unique_lock<std::shared_mutex> lock(m_sessionToGatewayMutex);
        m_sessionToGatewayId[sessionId] = gatewayId;
    }

    LOG_WRITE(LogLevel::Info, "LoginServer: gateway handshake complete. gatewayId=" + std::to_string(gatewayId) + " sessionId=" + std::to_string(sessionId));
}

// 로그인 성공 응답
void LoginServer::sendLoginSuccess(const netlib::ISessionPtr& spSession, int64 userId, uint64 authToken, const ServerInfo& gatewayInfo)
{
    GamePacket::LoginRes res;
    res.set_success(true);
    res.set_user_id(userId);
    res.set_auth_token(authToken);
    res.set_gateway_ip(gatewayInfo.ip);
    res.set_gateway_port(gatewayInfo.port);

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
    std::lock_guard<std::mutex> lock(m_gatewayClientsMutex);
    if (m_gatewayClients.contains(gatewayId))
        return;

    netlib::NetClientPtr spClient = ConnectToServer(ip, port, m_gatewayEventHandler);
    if (spClient)
    {
        m_gatewayClients[gatewayId] = spClient;
        LOG_WRITE(LogLevel::Info, "LoginServer: connecting to gateway " + std::to_string(gatewayId) + " " + ip + ":" + std::to_string(port));
    }
}

// 게이트웨이서버의 연결을 끊음
void LoginServer::disconnectFromGateway(int32 gatewayId)
{
    netlib::NetClientPtr spClient = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_gatewayClientsMutex);
        auto iter = m_gatewayClients.find(gatewayId);
        if (iter == m_gatewayClients.end())
            return;

        spClient = iter->second;
        m_gatewayClients.erase(iter);
    }

    if (spClient)
        DisconnectToServer(spClient);

    // m_gatewaySessions, m_sessionToGatewayId 정리는 onGatewayDisconnect 콜백에서 자동 처리됨

    LOG_WRITE(LogLevel::Info, "LoginServer: disconnected from gateway " + std::to_string(gatewayId));
}

std::optional<ServerInfo> LoginServer::selectGateway(int64 userId) const
{
    std::shared_lock<std::shared_mutex> lock(m_gatewayInfosMutex);

    if (m_gatewayInfos.empty())
        return std::nullopt;

    // 이전 접속 게이트웨이 우선 선택 (5분 TTL)
    {
        std::shared_lock<std::shared_mutex> prevLock(m_prevGatewayMutex);
        auto iter = m_prevGatewayMap.find(userId);
        if (iter != m_prevGatewayMap.end())
        {
            auto now = std::chrono::steady_clock::now();
            if (now < iter->second.expireTime)
            {
                int32 prevGatewayId = iter->second.gatewayServerId;
                auto iterInfo = m_gatewayInfos.find(prevGatewayId);
                if (iterInfo != m_gatewayInfos.end() &&
                    iterInfo->second.status == ServerStatus::Running)
                {
                    return iterInfo->second;
                }
            }
        }
    }

    // 유저 수가 가장 적은 Running 게이트웨이 선택
    const ServerInfo* pBestServer = nullptr;
    for (const auto& [id, info] : m_gatewayInfos)
    {
        if (info.status != ServerStatus::Running)
            continue;
        if (!pBestServer || info.userCount < pBestServer->userCount)
            pBestServer = &info;
    }

    return pBestServer ? std::optional<ServerInfo>(*pBestServer) : std::nullopt;
}

void LoginServer::sendAuthTokenToGateway(int32 gatewayId, int64 userId, uint64 authToken, int64 expireTimeMs)
{
    netlib::ISessionPtr spSession;
    {
        std::shared_lock<std::shared_mutex> lock(m_gatewaySessionsMutex);
        auto iter = m_gatewaySessions.find(gatewayId);
        if (iter == m_gatewaySessions.end())
        {
            LOG_WRITE(LogLevel::Warn, "LoginServer::sendAuthTokenToGateway - no session for gatewayId=" + std::to_string(gatewayId));
            return;
        }
        spSession = iter->second;
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
    {
        std::shared_lock<std::shared_mutex> lock(m_gatewaySessionsMutex);
        auto iter = m_gatewaySessions.find(gatewayId);
        if (iter == m_gatewaySessions.end())
            return;
        spSession = iter->second;
    }

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

    {
        std::unique_lock<std::shared_mutex> lock(m_loginMapMutex);
        m_loginMap[userId] = { userId, gatewayServerId };
    }

    // 이전 게이트웨이 정보 갱신 (5분 TTL)
    {
        std::unique_lock<std::shared_mutex> lock(m_prevGatewayMutex);
        m_prevGatewayMap[userId] = {
            gatewayServerId,
            now + std::chrono::milliseconds(k_prevGatewayTtlMs)
        };
    }
}

void LoginServer::removeLoginEntry(int64 userId)
{
    std::unique_lock<std::shared_mutex> lock(m_loginMapMutex);
    m_loginMap.erase(userId);
}

std::optional<LoginServer::LoginEntry> LoginServer::findLoginEntry(int64 userId) const
{
    std::shared_lock<std::shared_mutex> lock(m_loginMapMutex);
    auto iter = m_loginMap.find(userId);
    if (iter == m_loginMap.end())
        return std::nullopt;
    return iter->second;
}

void LoginServer::cleanupExpiredPrevGateway()
{
    auto now = std::chrono::steady_clock::now();

    std::unique_lock<std::shared_mutex> lock(m_prevGatewayMutex);
    for (auto iter = m_prevGatewayMap.begin(); iter != m_prevGatewayMap.end(); )
    {
        if (now >= iter->second.expireTime)
            iter = m_prevGatewayMap.erase(iter);
        else
            ++iter;
    }
}

// 게이트웨이서버에서 유저 접속끊김 노티를 받음
void LoginServer::handleUserDisconnectNtf(const netlib::ISessionPtr& /*spSession*/, const ServerPacket::UserDisconnectNtf& ntf)
{
    int64 userId = ntf.user_id();
    removeLoginEntry(userId);
    LOG_WRITE(LogLevel::Info, "LoginServer: user disconnected, removed from loginMap. userId=" + std::to_string(userId));
}

// 인증 토큰 생성
uint64 LoginServer::generateAuthToken()
{
    std::lock_guard<std::mutex> lock(m_rngMutex);
    return m_rng();
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
