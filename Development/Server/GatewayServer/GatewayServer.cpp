#include "pch.h"
#include "GatewayServer.h"


bool GatewayServer::OnInitialize()
{
    // 클라이언트 패킷 디스패처 등록
    m_clientDispatcher.Register<GamePacket::GatewayAuthReq>(Common::GAME_PACKET_ID_GATEWAY_AUTH_REQ,
        [this](auto& spClientSession, auto& msg) { handleAuthReq(spClientSession, msg); });

    m_clientDispatcher.Register<GamePacket::GameLogoutReq>(Common::GAME_PACKET_ID_GAME_LOGOUT_REQ,
        [this](auto& spClientSession, auto& msg) { handleLogoutReq(spClientSession); });

    // 게이트웨이서버가 핸들링하지않는 클라이언트 패킷은 게임서버로 relay
    m_clientDispatcher.SetUnknownPacketHandler([this](const netlib::ISessionPtr& spClientSession, const netlib::PacketPtr& spPacket)
    {
        relayToGameServer(spClientSession, spPacket);
    });

    // 게임서버 패킷 디스패처 등록
    m_gameServerDispatcher.Register<ServerPacket::GameServerHandshakeNtf>(Common::SERVER_PACKET_ID_GAME_SERVER_HANDSHAKE_NTF,
        [this](auto& spGameSession, auto& msg) { handleGameServerHandshake(spGameSession, msg); });

    m_gameServerDispatcher.Register<ServerPacket::GameToGatewayPacketNtf>(Common::SERVER_PACKET_ID_GAME_TO_GATEWAY_PACKET_NTF,
        [this](auto& spGameSession, auto& msg) { handleGameToGatewayPacket(spGameSession, msg); });

    m_gameServerDispatcher.Register<ServerPacket::GameToGatewayBroadcastNtf>(Common::SERVER_PACKET_ID_GAME_TO_GATEWAY_BROADCAST_NTF,
        [this](auto& spGameSession, auto& msg) { handleGameToGatewayBroadcast(spGameSession, msg); });

    m_gameServerDispatcher.Register<ServerPacket::UserMoveToGameServerReq>(Common::SERVER_PACKET_ID_USER_MOVE_TO_GAME_SERVER_REQ,
        [this](auto& spGameSession, auto& msg) { handleUserMoveToGameServer(spGameSession, msg); });

    m_gameServerDispatcher.SetUnknownPacketHandler([this](const netlib::ISessionPtr& spGameSession, const netlib::PacketPtr& spPacket)
    {
        LOG_WRITE(LogLevel::Warn, std::format("GatewayServer: unknown game server packetId={}, sessionId={}", spPacket->GetHeader()->type, spGameSession->GetId()));
    });

    // 로그인서버 패킷 디스패처 등록
    m_loginServerDispatcher.Register<ServerPacket::LoginAuthTokenNtf>(Common::SERVER_PACKET_ID_LOGIN_AUTH_TOKEN_NTF,
        [this](auto& spLoginSession, auto& msg) { handleLoginAuthTokenNtf(spLoginSession, msg); });

    m_loginServerDispatcher.Register<ServerPacket::LoginDuplicateNtf>(Common::SERVER_PACKET_ID_LOGIN_DUPLICATE_NTF,
        [this](auto& spLoginSession, auto& msg) { handleLoginDuplicateNtf(spLoginSession, msg); });

    m_loginServerDispatcher.SetUnknownPacketHandler([this](const netlib::ISessionPtr& spLoginSession, const netlib::PacketPtr& spPacket)
    {
        LOG_WRITE(LogLevel::Warn, std::format("GatewayServer: unknown login server packetId={}, sessionId={}", spPacket->GetHeader()->type, spLoginSession->GetId()));
    });

    // 클라이언트 포트 이벤트 핸들러 등록
    m_clientListenEventHandler.onAccept = [this](const netlib::ISessionPtr& spClientSession) { return onClientAccept(spClientSession); };
    m_clientListenEventHandler.onRecv = [this](const netlib::ISessionPtr& spClientSession, const netlib::PacketPtr& spPacket) { return onClientRecv(spClientSession, spPacket); };
    m_clientListenEventHandler.onDisconnect = [this](const netlib::ISessionPtr& spClientSession) { onClientDisconnect(spClientSession); };
    m_clientListenEventHandler.onLog = [](netlib::LogLevel netLogLevel, netlib::ISessionPtr, const std::string& msg)
    {
        const LogLevel logLevel = serverbase::NetLogLevelToLogLevel(netLogLevel);
        LOG_WRITE(logLevel, msg);
    };

    // 내부 서버 포트 이벤트 핸들러 등록
    m_internalListenEventHandler.onAccept = [this](const netlib::ISessionPtr& spServerSession) { return onInternalAccept(spServerSession); };
    m_internalListenEventHandler.onRecv = [this](const netlib::ISessionPtr& spServerSession, const netlib::PacketPtr& spPacket) { return onInternalRecv(spServerSession, spPacket); };
    m_internalListenEventHandler.onDisconnect = [this](const netlib::ISessionPtr& spServerSession) { onInternalDisconnect(spServerSession); };
    m_internalListenEventHandler.onLog = [](netlib::LogLevel netLogLevel, netlib::ISessionPtr spServerSession, const std::string& msg)
    {
        const LogLevel logLevel = serverbase::NetLogLevelToLogLevel(netLogLevel);
        LOG_WRITE(logLevel, msg);
    };

    // 1분 타이머
    GetTimer().Register(60000, [this]()
    {
        cleanupExpiredTokens();  // 만료된 인증토큰 제거
        cleanupExpiredPrevGameServer(); // 클라의 이전접속 게임서버정보 만료된거 제거
    });

    LOG_WRITE(LogLevel::Info, "GatewayServer::OnInitialize complete");
    return true;
}

// 레지스트리서버로부터 다른서버 정보를받음
void GatewayServer::OnServerInfoUpdated(const ServerInfo& info)
{
    if (info.serverType == ServerType::Game)
    {
        if (info.status == ServerStatus::Disconnected)
            m_safeGameServerInfos.Erase(info.serverId);
        else
            m_safeGameServerInfos.Insert(info.serverId, info);
    }
}

void GatewayServer::OnBeforeShutdown()
{
    LOG_WRITE(LogLevel::Info, "GatewayServer::OnBeforeShutdown");
}


// 세션에서 SessionMetaInfo를 꺼낸다.
// 주의: SessionMetaInfo* 를 다른곳에 보관해두면 안됨. 세션이 제거될때 함께 제거되기 때문
SessionMetaInfo* GatewayServer::getSessionMeta(const netlib::ISessionPtr& spSession)
{
    SessionMetaInfo* pMeta = static_cast<SessionMetaInfo*>(spSession->GetUserData().get());
    if (!pMeta)
    {
        LOG_WRITE(LogLevel::Error, std::format("SessionMetaInfo가 null입니다. sessionId={}", spSession->GetId()));
    }

    return pMeta;
}


// 클라이언트 accept
bool GatewayServer::onClientAccept(const netlib::ISessionPtr& spSession)
{
    if (IsShuttingDown())
        return false;

    spSession->SetUserData(std::make_shared<SessionMetaInfo>(ESessionType::Client));
    return true;
}

// 클라이언트에게 패킷받음
bool GatewayServer::onClientRecv(const netlib::ISessionPtr& spSession, const netlib::PacketPtr& spPacket)
{
    m_clientDispatcher.Dispatch(spSession, spPacket);
    return true;
}

// 클라이언트 연결끊김
void GatewayServer::onClientDisconnect(const netlib::ISessionPtr& spSession)
{
    SessionMetaInfo* pMeta = getSessionMeta(spSession);
    if (!pMeta)
        return;

    int64 userId = pMeta->userId;
    if (userId == 0)
        return;

    GatewayUserPtr spUser;
    m_safeUsers.EraseAndGet(userId, spUser);

    if (!spUser)
        return;

    LOG_WRITE(LogLevel::Info, std::format("GatewayServer: client disconnected. userId={}", userId));

    if (spUser->gameServerId != 0)
    {
        ServerPacket::GatewayUserDisconnectNtf ntf;
        ntf.set_user_id(userId);

        auto spPacket = SerializePacket(Common::SERVER_PACKET_ID_USER_DISCONNECT_NTF, ntf);
        if (spPacket)
            sendToGameServer(spUser->gameServerId, spPacket);
    }
}


// 내부서버 포트에서 accept 함
bool GatewayServer::onInternalAccept(const netlib::ISessionPtr& spSession)
{
    spSession->SetUserData(std::make_shared<SessionMetaInfo>(ESessionType::Unknown));
    return true;
}

// 내부서버 포트 패킷 recv
bool GatewayServer::onInternalRecv(const netlib::ISessionPtr& spSession, const netlib::PacketPtr& spPacket)
{
    SessionMetaInfo* pMeta = getSessionMeta(spSession);
    if (!pMeta)
        return true;

    switch (pMeta->sessionType)
    {
    case ESessionType::Unknown:
        if (spPacket->GetHeader()->type == Common::SERVER_PACKET_ID_GAME_SERVER_HANDSHAKE_NTF)
            handleGameServerPacket(spSession, spPacket);
        else
            handleLoginServerPacket(spSession, spPacket);
        break;

    case ESessionType::GameServer:
        handleGameServerPacket(spSession, spPacket);
        break;

    case ESessionType::LoginServer:
        handleLoginServerPacket(spSession, spPacket);
        break;

    default:
        break;
    }

    return true;
}

// 내부서버 포트에서 연결 끊김
void GatewayServer::onInternalDisconnect(const netlib::ISessionPtr& spSession)
{
    SessionMetaInfo* pMeta = getSessionMeta(spSession);
    if (!pMeta)
        return;

    if (pMeta->sessionType == ESessionType::GameServer)
    {
        int32 gameServerId = pMeta->gameServerId;
        if (gameServerId == 0)
            return;

        LOG_WRITE(LogLevel::Warn, std::format("GatewayServer: game server disconnected. gameServerId={}", gameServerId));

        m_safeGameServerSessions.Erase(gameServerId);

        std::vector<int64> affectedUsers = m_safeUsers.CollectKeys(
            [gameServerId](const int64&, const GatewayUserPtr& spUser)
            {
                return spUser->gameServerId == gameServerId;
            });

        for (int64 userId : affectedUsers)
            forceDisconnectUser(userId, "Game server disconnected");
    }
    else if (pMeta->sessionType == ESessionType::LoginServer)
    {
        LOG_WRITE(LogLevel::Warn, std::format("GatewayServer: login server disconnected. sessionId={}", spSession->GetId()));
    }
}


// 클라이언트 인증요청 처리
void GatewayServer::handleAuthReq(const netlib::ISessionPtr& spClientSession, const GamePacket::GatewayAuthReq& msg)
{
    // 이미 인증된 세션이면 비정상 요청
    SessionMetaInfo* pMeta = getSessionMeta(spClientSession);
    if (!pMeta || pMeta->userId != 0)
    {
        LOG_WRITE(LogLevel::Warn, std::format("GatewayServer: auth req on already authenticated session. sessionId={}", spClientSession->GetId()));
        spClientSession->Disconnect();
        return;
    }

    int64 userId = msg.user_id();
    uint64 authToken = msg.auth_token();

    if (!consumeAuthToken(userId, authToken))
    {
        LOG_WRITE(LogLevel::Warn, std::format("GatewayServer: auth failed. userId={}, sessionId={}", userId, spClientSession->GetId()));
        spClientSession->Disconnect();
        return;
    }

    // 중복 접속 처리
    GatewayUserPtr spExisting;
    if (m_safeUsers.Find(userId, spExisting))
    {
        LOG_WRITE(LogLevel::Info, std::format("GatewayServer: duplicate connection. userId={} disconnecting old session.", userId));
        spExisting->spClientSession->Disconnect();
    }

    // 게임서버 선택 (로드밸런싱)
    auto gameServer = selectGameServer(userId);
    if (!gameServer.has_value())
    {
        LOG_WRITE(LogLevel::Warn, std::format("GatewayServer: no available game server for userId={}", userId));
        spClientSession->Disconnect();
        return;
    }

    // 유저 객체 생성
    auto spUser = std::make_shared<GatewayUser>();
    spUser->userId = userId;
    spUser->gameServerId = gameServer->serverId;
    spUser->spClientSession = spClientSession;

    m_safeUsers.Insert(userId, spUser);

    pMeta->userId = userId;
    pMeta->routedGameServerId = gameServer->serverId;

    upsertPrevGameServer(userId, gameServer->serverId);

    LOG_WRITE(LogLevel::Info, std::format("GatewayServer: client authenticated. userId={}, gameServerId={}", userId, gameServer->serverId));

    // 게임서버에 유저 입장 알림
    ServerPacket::GatewayUserEnterNtf ntf;
    ntf.set_user_id(userId);
    ntf.set_gateway_id(GetServerId());
    ntf.set_client_ip(spUser->clientIp);

    auto spNtfPacket = SerializePacket(Common::SERVER_PACKET_ID_USER_ENTER_NTF, ntf);
    if (spNtfPacket)
        sendToGameServer(gameServer->serverId, spNtfPacket);
}

void GatewayServer::handleLogoutReq(const netlib::ISessionPtr& spClientSession)
{
    spClientSession->Disconnect();
}

void GatewayServer::relayToGameServer(const netlib::ISessionPtr& spClientSession, netlib::PacketPtr spPacket)
{
    SessionMetaInfo* pMeta = getSessionMeta(spClientSession);
    if (!pMeta || pMeta->userId == 0 || pMeta->routedGameServerId == 0)
        return;

    netlib::ISessionPtr spGameSession;
    if (!m_safeGameServerSessions.Find(pMeta->routedGameServerId, spGameSession))
        return;

    // 원본 클라 패킷에 userId를 Sidecar로 추가해서 그대로 게임서버로 전송한다.
    int64 userId = pMeta->userId;
    if (!spPacket->SetSidecar(&userId, sizeof(userId)))
    {
        LOG_WRITE(LogLevel::Error, std::format("GatewayServer: SetSidecar failed. userId={} packetType={}", userId, spPacket->GetHeader()->type));
        return;
    }

    spGameSession->Send(spPacket);
}


// 로그인서버 패킷 핸들러
void GatewayServer::handleLoginServerPacket(const netlib::ISessionPtr& spLoginSession, netlib::PacketPtr spPacket)
{
    SessionMetaInfo* pMeta = getSessionMeta(spLoginSession);
    if (pMeta && pMeta->sessionType == ESessionType::Unknown)
    {
        pMeta->sessionType = ESessionType::LoginServer;

        ServerPacket::GatewayHandshakeNtf handshake;
        handshake.set_server_id(GetServerId());

        auto spHandshakePacket = SerializePacket(Common::SERVER_PACKET_ID_LOGIN_GATEWAY_HANDSHAKE_NTF, handshake);
        if (spHandshakePacket)
            spLoginSession->Send(spHandshakePacket);

        LOG_WRITE(LogLevel::Info, std::format("GatewayServer: login server connected. sent handshake. sessionId={}", spLoginSession->GetId()));
    }

    m_loginServerDispatcher.Dispatch(spLoginSession, spPacket);
}

void GatewayServer::handleLoginAuthTokenNtf(const netlib::ISessionPtr& /*spLoginSession*/, const ServerPacket::LoginAuthTokenNtf& msg)
{
    storeAuthToken(msg.user_id(), msg.auth_token(), msg.expire_time_ms());
    LOG_WRITE(LogLevel::Info, std::format("GatewayServer: auth token stored. userId={}", msg.user_id()));
}

void GatewayServer::handleLoginDuplicateNtf(const netlib::ISessionPtr& /*spLoginSession*/, const ServerPacket::LoginDuplicateNtf& msg)
{
    int64 userId = msg.user_id();
    LOG_WRITE(LogLevel::Info, std::format("GatewayServer: duplicate login notified. userId={}", userId));
    forceDisconnectUser(userId, "Duplicate login");
}


// 게임서버 패킷 핸들러
void GatewayServer::handleGameServerPacket(const netlib::ISessionPtr& spGameSession, netlib::PacketPtr spPacket)
{
    m_gameServerDispatcher.Dispatch(spGameSession, spPacket);
}

void GatewayServer::handleGameServerHandshake(const netlib::ISessionPtr& spGameSession, const ServerPacket::GameServerHandshakeNtf& msg)
{
    int32 gameServerId = msg.server_id();

    m_safeGameServerSessions.Insert(gameServerId, spGameSession);

    SessionMetaInfo* pMeta = getSessionMeta(spGameSession);
    if (pMeta)
    {
        pMeta->sessionType = ESessionType::GameServer;
        pMeta->gameServerId = gameServerId;
    }

    LOG_WRITE(LogLevel::Info, std::format("GatewayServer: game server handshake complete. gameServerId={}", gameServerId));
}

void GatewayServer::handleGameToGatewayPacket(const netlib::ISessionPtr& /*spGameSession*/, const ServerPacket::GameToGatewayPacketNtf& msg)
{
    int64 userId = msg.user_id();

    GatewayUserPtr spUser;
    if (!m_safeUsers.Find(userId, spUser) || !spUser->spClientSession)
        return;

    auto spPacket = AllocPacket();
    if (!spPacket)
        return;

    const std::string& payload = msg.payload();
    spPacket->SetHeader(
        static_cast<uint16>(sizeof(netlib::PacketHeader) + payload.size()),
        static_cast<uint16>(msg.packet_type()),
        netlib::PacketFlags::None
    );
    std::memcpy(spPacket->GetPayload(), payload.data(), payload.size());

    spUser->spClientSession->Send(spPacket);
}

void GatewayServer::handleGameToGatewayBroadcast(const netlib::ISessionPtr& /*spGameSession*/, const ServerPacket::GameToGatewayBroadcastNtf& msg)
{
    const std::string& payload = msg.payload();
    auto spPacket = AllocPacket();
    if (!spPacket)
        return;

    spPacket->SetHeader(
        static_cast<uint16>(sizeof(netlib::PacketHeader) + payload.size()),
        static_cast<uint16>(msg.packet_type()),
        netlib::PacketFlags::None
    );
    std::memcpy(spPacket->GetPayload(), payload.data(), payload.size());

    m_safeUsers.ForEach([&](const int64& userId, const GatewayUserPtr& spUser)
    {
        for (int i = 0; i < msg.user_ids_size(); ++i)
        {
            if (msg.user_ids(i) == userId && spUser->spClientSession)
            {
                spUser->spClientSession->Send(spPacket);
                break;
            }
        }
    });
}

void GatewayServer::handleUserMoveToGameServer(const netlib::ISessionPtr& /*spGameSession*/, const ServerPacket::UserMoveToGameServerReq& msg)
{
    int64 userId             = msg.user_id();
    int32 targetGameServerId = msg.target_game_server_id();
    int32 targetStageId      = msg.target_stage_id();

    GatewayUserPtr spUser;
    if (!m_safeUsers.Find(userId, spUser))
        return;

    netlib::ISessionPtr spTargetSession;
    if (!m_safeGameServerSessions.Find(targetGameServerId, spTargetSession))
    {
        LOG_WRITE(LogLevel::Warn, std::format("GatewayServer: target game server not found. targetGameServerId={}, userId={}", targetGameServerId, userId));

        ServerPacket::UserMoveToGameServerFailNtf failNtf;
        failNtf.set_user_id(userId);
        failNtf.set_reason("Target game server not found");

        auto spFailPacket = SerializePacket(Common::SERVER_PACKET_ID_USER_MOVE_TO_GAME_SERVER_FAIL_NTF, failNtf);
        if (spFailPacket)
            sendToGameServer(spUser->gameServerId, spFailPacket);
        return;
    }

    spUser->gameServerId = targetGameServerId;
    m_safeUsers.Insert(userId, spUser);

    SessionMetaInfo* pMeta = getSessionMeta(spUser->spClientSession);
    if (pMeta)
        pMeta->routedGameServerId = targetGameServerId;

    upsertPrevGameServer(userId, targetGameServerId);

    LOG_WRITE(LogLevel::Info, std::format("GatewayServer: user rerouted. userId={} -> gameServerId={}", userId, targetGameServerId));

    ServerPacket::GatewayUserRerouteNtf rerouteNtf;
    rerouteNtf.set_user_id(userId);
    rerouteNtf.set_gateway_id(GetServerId());
    rerouteNtf.set_target_stage_id(targetStageId);
    rerouteNtf.set_client_ip(spUser->clientIp);

    auto spReroutePacket = SerializePacket(Common::SERVER_PACKET_ID_USER_REROUTE_NTF, rerouteNtf);
    if (spReroutePacket)
        spTargetSession->Send(spReroutePacket);
}


// 인증토큰 저장
void GatewayServer::storeAuthToken(int64 userId, uint64 authToken, int64 expireTimeMs)
{
    m_safeAuthTokens.Insert(userId, { authToken, expireTimeMs });
}

// 인증토큰 소모
bool GatewayServer::consumeAuthToken(int64 userId, uint64 authToken)
{
    int64 nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    AuthTokenEntry entry;
    if (!m_safeAuthTokens.EraseAndGet(userId, entry))
        return false;

    if (entry.authToken != authToken || nowMs > entry.expireTimeMs)
        return false;

    return true;
}

// 만료된 인증토큰 제거
void GatewayServer::cleanupExpiredTokens()
{
    int64 nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    std::vector<int64> expiredKeys = m_safeAuthTokens.CollectKeys(
        [nowMs](const int64&, const AuthTokenEntry& entry)
        {
            return nowMs > entry.expireTimeMs;
        });

    m_safeAuthTokens.Erase(expiredKeys);
}


// 이전 접속 게임서버 업데이트
void GatewayServer::upsertPrevGameServer(int64 userId, int32 gameServerId)
{
    auto expireTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(k_prevGameServerTtlMs);
    m_safePrevGameServer.Insert(userId, { gameServerId, expireTime });
}

// 이전 접속 게임서버정보 만료된거 제거
void GatewayServer::cleanupExpiredPrevGameServer()
{
    auto now = std::chrono::steady_clock::now();

    std::vector<int64> expiredKeys = m_safePrevGameServer.CollectKeys(
        [now](const int64&, const PrevGameServerEntry& entry)
        {
            return now >= entry.expireTime;
        });

    m_safePrevGameServer.Erase(expiredKeys);
}

std::optional<ServerInfo> GatewayServer::selectGameServer(int64 userId) const
{
    if (m_safeGameServerInfos.Empty())
        return std::nullopt;

    // 이전 접속 게임서버 우선 선택 (5분 TTL)
    PrevGameServerEntry prevEntry;
    if (m_safePrevGameServer.Find(userId, prevEntry))
    {
        if (std::chrono::steady_clock::now() < prevEntry.expireTime)
        {
            ServerInfo prevInfo;
            if (m_safeGameServerInfos.Find(prevEntry.gameServerId, prevInfo))
            {
                if (prevInfo.status == ServerStatus::Running)
                    return prevInfo;
            }
        }
    }

    // 유저 수가 가장 적은 Running 게임서버 선택
    std::optional<ServerInfo> best;
    m_safeGameServerInfos.ForEach([&](const int32&, const ServerInfo& info)
    {
        if (info.status != ServerStatus::Running)
            return;

        if (!best.has_value() || info.userCount < best->userCount)
            best = info;
    });

    return best;
}


// 게임서버로 서버간 패킷 전달
void GatewayServer::sendToGameServer(int32 gameServerId, netlib::PacketPtr spPacket)
{
    netlib::ISessionPtr spSession;
    if (!m_safeGameServerSessions.Find(gameServerId, spSession))
    {
        LOG_WRITE(LogLevel::Warn, std::format("GatewayServer::sendToGameServer - no session for gameServerId={}", gameServerId));
        return;
    }

    spSession->Send(spPacket);
}

void GatewayServer::forceDisconnectUser(int64 userId, const std::string& reason)
{
    GatewayUserPtr spUser;
    if (!m_safeUsers.Find(userId, spUser) || !spUser->spClientSession)
        return;

    LOG_WRITE(LogLevel::Info, std::format("GatewayServer: force disconnecting userId={}, reason={}", userId, reason));

    GamePacket::ForceDisconnectNtf ntf;
    ntf.set_reason_code(GamePacket::FORCE_DISCONNECT_REASON_SERVER_SHUTDOWN);
    ntf.set_message(reason);

    auto spPacket = SerializePacket(Common::GAME_PACKET_ID_FORCE_DISCONNECT_NTF, ntf);
    if (spPacket)
        spUser->spClientSession->Send(spPacket);

    spUser->spClientSession->Disconnect();
}
