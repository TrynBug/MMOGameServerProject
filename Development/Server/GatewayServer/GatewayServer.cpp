#include "pch.h"
#include "GatewayServer.h"


bool GatewayServer::OnInitialize()
{
    if (IsMetricsEnabled())
    {
        auto& registry = GetMetricsRegistry();
        // container/session 크기는 scrape 직전에 안전한 Size()로 읽는 현재 상태 Gauge다.
        // auth/route/reroute 결과는 event 처리 지점에서 증가하는 Counter이며 direction/result는 가능한 값이 고정된 label이다.
        bool registered = true;
        registered &= registry.AddGauge(serverbase::GaugeMetric::Gateway_Users, "mmo_gateway_users", "Current authenticated Gateway users.");
        registered &= registry.AddGauge(serverbase::GaugeMetric::Gateway_AuthTokens, "mmo_gateway_auth_tokens", "Unconsumed authentication tokens.");
        registered &= registry.AddGauge(serverbase::GaugeMetric::Gateway_PreviousGameServerCache, "mmo_gateway_prev_server_cache", "Cached previous GameServer routes.");
        registered &= registry.AddGauge(serverbase::GaugeMetric::Gateway_GameServerConnections, "mmo_gateway_game_server_connections", "Authenticated GameServer connections.");
        registered &= registry.AddGauge(serverbase::GaugeMetric::Gateway_KnownGameServers, "mmo_gateway_known_game_servers", "GameServers currently known from Registry.");
        registered &= registry.AddCounter(serverbase::CounterMetric::Gateway_AuthSuccess, "mmo_gateway_auth_total", "Gateway authentication results.", { { "result", "success" } });
        registered &= registry.AddCounter(serverbase::CounterMetric::Gateway_AuthFailure, "mmo_gateway_auth_total", "Gateway authentication results.", { { "result", "failure" } });
        registered &= registry.AddCounter(serverbase::CounterMetric::Gateway_DuplicateLogin, "mmo_gateway_duplicate_login_total", "Duplicate-login notifications and authenticated reconnects.");
        registered &= registry.AddCounter(serverbase::CounterMetric::Gateway_ClientToGameRouteSuccess, "mmo_gateway_route_total", "Gateway routing results.", { { "direction", "client_to_game" }, { "result", "success" } });
        registered &= registry.AddCounter(serverbase::CounterMetric::Gateway_ClientToGameRouteMissing, "mmo_gateway_route_total", "Gateway routing results.", { { "direction", "client_to_game" }, { "result", "missing_route" } });
        registered &= registry.AddCounter(serverbase::CounterMetric::Gateway_GameToClientRouteSuccess, "mmo_gateway_route_total", "Gateway routing results.", { { "direction", "game_to_client" }, { "result", "success" } });
        registered &= registry.AddCounter(serverbase::CounterMetric::Gateway_GameToClientRouteMissingUser, "mmo_gateway_route_total", "Gateway routing results.", { { "direction", "game_to_client" }, { "result", "missing_user" } });
        registered &= registry.AddCounter(serverbase::CounterMetric::Gateway_RerouteSuccess, "mmo_gateway_reroute_total", "Cross-GameServer reroute results.", { { "result", "success" } });
        registered &= registry.AddCounter(serverbase::CounterMetric::Gateway_RerouteFailure, "mmo_gateway_reroute_total", "Cross-GameServer reroute results.", { { "result", "failure" } });
        if (!registered)
            return false;
    }

    // 클라이언트 패킷 디스패처 등록
    m_clientDispatcher.Register<GamePacket::GatewayAuthReq>(Common::GAME_PACKET_ID_GATEWAY_AUTH_REQ,
        [this](auto& spClientSession, auto& msg) { handleAuthReq(spClientSession, msg); });

    m_clientDispatcher.Register<GamePacket::GameLogoutReq>(Common::GAME_PACKET_ID_GAME_LOGOUT_REQ,
        [this](auto& spClientSession, auto& msg) { handleLogoutReq(spClientSession); });

    m_clientDispatcher.Register<GamePacket::LatencyProbeReq>(Common::GAME_PACKET_ID_GATEWAY_LATENCY_PROBE_REQ,
        [this](auto& spClientSession, auto& msg) { handleLatencyProbeReq(spClientSession, msg); });

    // 게이트웨이서버가 핸들링하지않는 클라이언트 패킷은 게임서버로 relay
    m_clientDispatcher.SetUnknownPacketHandler([this](const netlib::ISessionPtr& spClientSession, const netlib::PacketPtr& spPacket)
    {
        relayToGameServer(spClientSession, spPacket);
    });

    // 게임서버 패킷 디스패처 등록
    // (클라 전달용 패킷은 sidecar 로 식별하여 handleGameServerPacket 에서 직접 처리하므로 디스패처에 등록하지 않는다.)
    m_gameServerDispatcher.Register<ServerPacket::ServerHandshakeReq>(Common::SERVER_PACKET_ID_SERVER_HANDSHAKE_REQ,
        [this](auto& spGameSession, auto& msg) { handleGameServerHandshakeReq(spGameSession, msg); });

    m_gameServerDispatcher.Register<ServerPacket::UserMoveToGameServerReq>(Common::SERVER_PACKET_ID_USER_MOVE_TO_GAME_SERVER_REQ,
        [this](auto& spGameSession, auto& msg) { handleUserMoveToGameServer(spGameSession, msg); });

    m_gameServerDispatcher.Register<ServerPacket::SetClientLatencyReq>(Common::SERVER_PACKET_ID_SET_CLIENT_LATENCY_REQ,
        [this](auto& spGameSession, auto& msg) { handleSetClientLatency(spGameSession, msg); });

    m_gameServerDispatcher.SetUnknownPacketHandler([this](const netlib::ISessionPtr& spGameSession, const netlib::PacketPtr& spPacket)
    {
        LOG_WRITE(LogLevel::Warn, std::format("unknown game server packetId={}, sessionId={}", spPacket->GetHeader()->type, spGameSession->GetId()));
    });

    // 로그인서버 패킷 디스패처 등록
    m_loginServerDispatcher.Register<ServerPacket::ServerHandshakeReq>(Common::SERVER_PACKET_ID_SERVER_HANDSHAKE_REQ,
        [this](auto& spLoginSession, auto& msg) { handleLoginServerHandshakeReq(spLoginSession, msg); });

    m_loginServerDispatcher.Register<ServerPacket::LoginAuthTokenNtf>(Common::SERVER_PACKET_ID_LOGIN_AUTH_TOKEN_NTF,
        [this](auto& spLoginSession, auto& msg) { handleLoginAuthTokenNtf(spLoginSession, msg); });

    m_loginServerDispatcher.Register<ServerPacket::LoginDuplicateNtf>(Common::SERVER_PACKET_ID_LOGIN_DUPLICATE_NTF,
        [this](auto& spLoginSession, auto& msg) { handleLoginDuplicateNtf(spLoginSession, msg); });

    m_loginServerDispatcher.SetUnknownPacketHandler([this](const netlib::ISessionPtr& spLoginSession, const netlib::PacketPtr& spPacket)
    {
        LOG_WRITE(LogLevel::Warn, std::format("unknown login server packetId={}, sessionId={}", spPacket->GetHeader()->type, spLoginSession->GetId()));
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

    LOG_WRITE(LogLevel::Info, "complete");
    return true;
}

void GatewayServer::OnMetricsCollect()
{
    // monitoring worker가 container 내부를 순회하지 않고 thread-safe Size()만 읽어 scrape 비용을 현재 사용자 수와 무관하게 유지한다.
    // known GameServer와 authenticated connection을 분리해 Registry 발견 문제와 실제 연결 문제를 구분한다.
    auto& registry = GetMetricsRegistry();
    registry.Set(serverbase::GaugeMetric::Gateway_Users, static_cast<double>(m_safeUsers.Size()));
    registry.Set(serverbase::GaugeMetric::Gateway_AuthTokens, static_cast<double>(m_safeAuthTokens.Size()));
    registry.Set(serverbase::GaugeMetric::Gateway_PreviousGameServerCache, static_cast<double>(m_safePrevGameServer.Size()));
    registry.Set(serverbase::GaugeMetric::Gateway_GameServerConnections, static_cast<double>(m_safeGameServerSessions.Size()));
    registry.Set(serverbase::GaugeMetric::Gateway_KnownGameServers, static_cast<double>(m_safeGameServerInfos.Size()));
}

// 레지스트리서버로부터 다른서버 정보를받음
void GatewayServer::OnServerInfoUpdated(const ServerInfo& info)
{
    if (info.serverType == ServerType::Game)
    {
        std::lock_guard<std::mutex> lock(m_gameServerSelectionMutex);

        // 새 Registry 스냅샷에는 이전 선택 결과가 반영됐다고 보고 예약값을 초기화한다.
        if (info.status == ServerStatus::Disconnected)
        {
            m_safeGameServerInfos.Erase(info.serverId);
            m_gameServerPendingAssignments.erase(info.serverId);
        }
        else
        {
            m_safeGameServerInfos.Insert(info.serverId, info);
            m_gameServerPendingAssignments[info.serverId] = 0;
        }
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

// 세션에서 InternalSessionMeta를 꺼낸다.
// 주의: InternalSessionMeta* 를 다른곳에 보관해두면 안됨. 세션이 제거될때 함께 제거되기 때문
InternalSessionMeta* GatewayServer::getInternalSessionMeta(const netlib::ISessionPtr& spSession)
{
    return static_cast<InternalSessionMeta*>(spSession->GetUserData().get());
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

    int64 accountId = pMeta->accountId;
    if (accountId == 0)
        return;

    GatewayUserPtr spUser;
    m_safeUsers.EraseAndGet(accountId, spUser);

    if (!spUser)
        return;

    GetRegistryClient()->SetUserCount(static_cast<int32>(m_safeUsers.Size()));

    LOG_WRITE(LogLevel::Info, std::format("client disconnected. accountId={}", accountId));

    if (spUser->gameServerId != 0)
    {
        ServerPacket::GatewayUserDisconnectNtf ntf;
        ntf.set_account_id(accountId);

        auto spPacket = SerializePacket(Common::SERVER_PACKET_ID_USER_DISCONNECT_NTF, ntf);
        if (spPacket)
            sendToGameServer(spUser->gameServerId, spPacket);
    }
}


// 내부서버 포트에서 accept 함
bool GatewayServer::onInternalAccept(const netlib::ISessionPtr& spSession)
{
    spSession->SetUserData(std::make_shared<InternalSessionMeta>());
    return true;
}

// 내부서버 포트 패킷 recv
bool GatewayServer::onInternalRecv(const netlib::ISessionPtr& spSession, const netlib::PacketPtr& spPacket)
{
    InternalSessionMeta* pMeta = getInternalSessionMeta(spSession);
    switch (pMeta->peerServerType)
    {
    case ServerType::Unknown:
    {
        // 서버타입이 Unknown 이면 Accept는 했는데 아직 handshake를 주고받지 않은 경우이다.
        // 그리고 Accept한 다음 처음으로 받은 패킷은 반드시 handshake req 패킷이어야 한다.
        if (spPacket->GetHeader()->type != Common::SERVER_PACKET_ID_SERVER_HANDSHAKE_REQ)
        {
            LOG_WRITE(LogLevel::Error, std::format("Accept 후 처음 받는 패킷이 handshake req가 아님. PacketType={}, IP={}:{}", spPacket->GetHeader()->type, spSession->GetIP(), spSession->GetPort()));
            return false;
        }

        // ServerHandshakeReq 메시지 확인
        ServerPacket::ServerHandshakeReq req;
        if (!DeserializePacket(*spPacket, req))
        {
            LOG_WRITE(LogLevel::Error, std::format("failed to deserialize ServerHandshakeReq, IP={}:{}", spSession->GetIP(), spSession->GetPort()));
            return false;
        }

        // meta에 ServerType을 입력해준다.
        pMeta->peerServerType = static_cast<ServerType>(req.server_type());
        if (ServerType::Unknown == pMeta->peerServerType)
        {
            LOG_WRITE(LogLevel::Error, std::format("ServerHandshakeReq ServerType이 Unknown 입니다. ServerType={}, IP={}:{}", req.server_type(), spSession->GetIP(), spSession->GetPort()));
            return false;
        }

        // onInternalRecv 한번 더 호출해줌
        return onInternalRecv(spSession, spPacket);
    }

    case ServerType::Game:
    {
        handleGameServerPacket(spSession, spPacket);
        break;
    }

    case ServerType::Login:
    {
        handleLoginServerPacket(spSession, spPacket);
        break;
    }

    default:
    {
        LOG_WRITE(LogLevel::Error, std::format("Invalid ServerType, ServerType={}, IP={}:{}", static_cast<int32>(pMeta->peerServerType), spSession->GetIP(), spSession->GetPort()));
        break;
    }
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

        LOG_WRITE(LogLevel::Warn, std::format("game server disconnected. gameServerId={}", gameServerId));

        {
            std::lock_guard<std::mutex> lock(m_gameServerSelectionMutex);
            m_safeGameServerSessions.Erase(gameServerId);
        }

        std::vector<int64> affectedUsers = m_safeUsers.CollectKeys(
            [gameServerId](const int64&, const GatewayUserPtr& spUser)
            {
                return spUser->gameServerId == gameServerId;
            });

        for (int64 accountId : affectedUsers)
            forceDisconnectUser(accountId, "Game server disconnected");
    }
    else if (pMeta->sessionType == ESessionType::LoginServer)
    {
        LOG_WRITE(LogLevel::Warn, std::format("login server disconnected. sessionId={}", spSession->GetId()));
    }
}


// 클라이언트 인증요청 처리
void GatewayServer::handleAuthReq(const netlib::ISessionPtr& spClientSession, const GamePacket::GatewayAuthReq& msg)
{
    // 이미 인증된 세션이면 비정상 요청
    SessionMetaInfo* pMeta = getSessionMeta(spClientSession);
    if (!pMeta || pMeta->accountId != 0)
    {
        GetMetricsRegistry().Inc(serverbase::CounterMetric::Gateway_AuthFailure);
        LOG_WRITE(LogLevel::Warn, std::format("auth req on already authenticated session. sessionId={}", spClientSession->GetId()));
        spClientSession->Disconnect();
        return;
    }

    int64 accountId = msg.account_id();
    uint64 authToken = msg.auth_token();

    if (!consumeAuthToken(accountId, authToken))
    {
        GetMetricsRegistry().Inc(serverbase::CounterMetric::Gateway_AuthFailure);
        LOG_WRITE(LogLevel::Warn, std::format("auth failed. accountId={}, sessionId={}", accountId, spClientSession->GetId()));
        spClientSession->Disconnect();
        return;
    }

    // 중복 접속 처리
    GatewayUserPtr spExisting;
    if (m_safeUsers.Find(accountId, spExisting))
    {
        GetMetricsRegistry().Inc(serverbase::CounterMetric::Gateway_DuplicateLogin);
        LOG_WRITE(LogLevel::Info, std::format("duplicate connection. accountId={} disconnecting old session.", accountId));
        spExisting->spClientSession->Disconnect();
    }

    // 게임서버 선택 (로드밸런싱)
    auto gameServer = selectGameServer(accountId);
    if (!gameServer.has_value())
    {
        GetMetricsRegistry().Inc(serverbase::CounterMetric::Gateway_AuthFailure);
        LOG_WRITE(LogLevel::Warn, std::format("no available game server for accountId={}", accountId));
        spClientSession->Disconnect();
        return;
    }

    // 게임서버에 유저 입장 알림. 선택 직후 세션이 사라졌으면 인증을 완료하지 않는다.
    auto spUser = std::make_shared<GatewayUser>();
    spUser->accountId = accountId;
    spUser->gameServerId = gameServer->serverId;
    spUser->spClientSession = spClientSession;

    ServerPacket::GatewayUserEnterNtf ntf;
    ntf.set_account_id(accountId);
    ntf.set_gateway_id(GetServerId());
    ntf.set_client_ip(spUser->clientIp);

    auto spNtfPacket = SerializePacket(Common::SERVER_PACKET_ID_USER_ENTER_NTF, ntf);
    if (!spNtfPacket || !sendToGameServer(gameServer->serverId, spNtfPacket))
    {
        {
            std::lock_guard<std::mutex> lock(m_gameServerSelectionMutex);
            auto iter = m_gameServerPendingAssignments.find(gameServer->serverId);
            if (iter != m_gameServerPendingAssignments.end() && iter->second > 0)
                --iter->second;
        }

        GetMetricsRegistry().Inc(serverbase::CounterMetric::Gateway_AuthFailure);
        LOG_WRITE(LogLevel::Warn, std::format("selected game server became unavailable. accountId={} gameServerId={}", accountId, gameServer->serverId));
        spClientSession->Disconnect();
        return;
    }

    m_safeUsers.Insert(accountId, spUser);
    GetRegistryClient()->SetUserCount(static_cast<int32>(m_safeUsers.Size()));
    GetMetricsRegistry().Inc(serverbase::CounterMetric::Gateway_AuthSuccess);

    pMeta->accountId = accountId;
    pMeta->routedGameServerId = gameServer->serverId;

    upsertPrevGameServer(accountId, gameServer->serverId);

    LOG_WRITE(LogLevel::Info, std::format("client authenticated. accountId={}, gameServerId={}", accountId, gameServer->serverId));
}

void GatewayServer::handleLogoutReq(const netlib::ISessionPtr& spClientSession)
{
    spClientSession->Disconnect();
}

void GatewayServer::handleLatencyProbeReq(const netlib::ISessionPtr& spClientSession, const GamePacket::LatencyProbeReq& msg)
{
    const SessionMetaInfo* pMeta = getSessionMeta(spClientSession);
    if (!pMeta || pMeta->accountId == 0)
        return;

    GamePacket::LatencyProbeRes res;
    res.set_sequence(msg.sequence());
    netlib::PacketPtr spResponse = SerializePacket(Common::GAME_PACKET_ID_GATEWAY_LATENCY_PROBE_RES, res);
    if (spResponse)
        spClientSession->Send(spResponse);
}

void GatewayServer::relayToGameServer(const netlib::ISessionPtr& spClientSession, const netlib::PacketPtr& spPacket)
{
    SessionMetaInfo* pMeta = getSessionMeta(spClientSession);
    if (!pMeta || pMeta->accountId == 0 || pMeta->routedGameServerId == 0)
    {
        GetMetricsRegistry().Inc(serverbase::CounterMetric::Gateway_ClientToGameRouteMissing);
        return;
    }

    netlib::ISessionPtr spGameSession;
    if (!m_safeGameServerSessions.Find(pMeta->routedGameServerId, spGameSession))
    {
        GetMetricsRegistry().Inc(serverbase::CounterMetric::Gateway_ClientToGameRouteMissing);
        return;
    }

    // 클라이언트 수신 Packet은 SidecarHeader + accountId 공간을 미리 확보되어 있다.
    int64 accountId = pMeta->accountId;
    if (!spPacket->SetSidecar(&accountId, sizeof(accountId)))
    {
        GetMetricsRegistry().Inc(serverbase::CounterMetric::Gateway_ClientToGameRouteMissing);
        LOG_WRITE(LogLevel::Error, std::format("SetSidecar failed. accountId={} packetType={}", accountId, spPacket->GetHeader()->type));
        return;
    }

    spGameSession->Send(spPacket);
    GetMetricsRegistry().Inc(serverbase::CounterMetric::Gateway_ClientToGameRouteSuccess);
}


// 로그인서버 패킷 핸들러
void GatewayServer::handleLoginServerPacket(const netlib::ISessionPtr& spLoginSession, const netlib::PacketPtr& spPacket)
{
    // 공용 핸드셰이크는 ServerBase가 직접 처리하므로
    // 이 지점에 온 패킷은 핸드셰이크 완료 후의 일반 패킷이다.
    m_loginServerDispatcher.Dispatch(spLoginSession, spPacket);
}

void GatewayServer::handleLoginServerHandshakeReq(const netlib::ISessionPtr& spLoginSession, const ServerPacket::ServerHandshakeReq& msg)
{
    InternalSessionMeta* pMeta = getInternalSessionMeta(spLoginSession);
    if (!pMeta)
        return;

    int32 loginServerId = msg.server_id();
    ServerType loginServerType = static_cast<ServerType>(msg.server_type());
    if (ServerType::Login != loginServerType)
    {
        LOG_WRITE(LogLevel::Error, std::format("login server handshake invalid server type. loginServerId={}, serverType={}", loginServerId, msg.server_type()));
        return;
    }

    if (pMeta->handshakeDone)
    {
        LOG_WRITE(LogLevel::Error, std::format("duplicated login server handshake request. loginServerId={}", loginServerId));
        return;
    }

    pMeta->handshakeDone = true;
    pMeta->isConnector = false;
    pMeta->peerServerId = loginServerId;
    pMeta->peerServerType = loginServerType;

    // 로그인서버에 Handshake Res 전송
    ServerPacket::ServerHandshakeRes res;
    res.set_success(true);
    res.set_server_id(m_serverId);
    netlib::PacketPtr spPacket = SerializePacket(Common::SERVER_PACKET_ID_SERVER_HANDSHAKE_RES, res);
    spLoginSession->Send(spPacket);

    LOG_WRITE(LogLevel::Info, std::format("login server handshake complete. loginServerId={}", loginServerId));
}

void GatewayServer::handleLoginAuthTokenNtf(const netlib::ISessionPtr& /*spLoginSession*/, const ServerPacket::LoginAuthTokenNtf& msg)
{
    storeAuthToken(msg.account_id(), msg.auth_token(), msg.expire_time_ms());
    LOG_WRITE(LogLevel::Info, std::format("auth token stored. accountId={}", msg.account_id()));
}

void GatewayServer::handleLoginDuplicateNtf(const netlib::ISessionPtr& /*spLoginSession*/, const ServerPacket::LoginDuplicateNtf& msg)
{
    GetMetricsRegistry().Inc(serverbase::CounterMetric::Gateway_DuplicateLogin);
    int64 accountId = msg.account_id();
    LOG_WRITE(LogLevel::Info, std::format("duplicate login notified. accountId={}", accountId));
    forceDisconnectUser(accountId, "Duplicate login");
}


// 게임서버 패킷 핸들러
void GatewayServer::handleGameServerPacket(const netlib::ISessionPtr& spGameSession, const netlib::PacketPtr& spPacket)
{
    // sidecar 가 있으면 클라 전달용 패킷이다. (게임서버가 수신자 accountId 목록을 sidecar 로 붙여서 보냄)
    // 그 외는 서버간 통신 패킷이므로 디스패처로 처리.
    if (spPacket->HasSidecar())
    {
        forwardClientPacket(spPacket);
        return;
    }

    m_gameServerDispatcher.Dispatch(spGameSession, spPacket);
}

// 게임서버가 보낸 클라 전달용 패킷을 대상 유저(들)에게 전달한다.
// sidecar 에 수신자 accountId 목록(int64 배열)이 들어있다. sidecar 를 떼어내 깨끗한 클라 패킷으로 만든 뒤,
// 같은 버퍼를 대상 유저들에게 전송한다(브로드캐스트 시 버퍼 1개 공유).
void GatewayServer::forwardClientPacket(const netlib::PacketPtr& spPacket)
{
    const int32 sidecarSize = spPacket->GetSidecarSize();
    const int32 count = sidecarSize / static_cast<int32>(sizeof(int64));
    if (count <= 0)
    {
        LOG_WRITE(LogLevel::Warn, std::format("forwardClientPacket: invalid sidecar size={}", sidecarSize));
        return;
    }

    // accountId 목록을 미리 복사한다 (StripSidecar 후 sidecar 영역이 사라지므로).
    // thread_local 재사용 버퍼 — 게이트웨이 IOCP 워커가 전달 패킷마다 vector 를 새로 만들지 않게 한다.
    thread_local std::vector<int64> accountIds;
    accountIds.resize(count);
    std::memcpy(accountIds.data(), spPacket->GetSidecarData(), static_cast<size_t>(count) * sizeof(int64));

    // sidecar 제거 → [PacketHeader][payload] 로 복원 (클라는 sidecar 를 모른다).
    spPacket->StripSidecar();

    for (int64 accountId : accountIds)
    {
        GatewayUserPtr spUser;
        if (!m_safeUsers.Find(accountId, spUser) || !spUser->spClientSession)
        {
            GetMetricsRegistry().Inc(serverbase::CounterMetric::Gateway_GameToClientRouteMissingUser);
            continue;
        }

        spUser->spClientSession->Send(spPacket);
        GetMetricsRegistry().Inc(serverbase::CounterMetric::Gateway_GameToClientRouteSuccess);
    }
}

// 게임서버의 handshake req 처리
void GatewayServer::handleGameServerHandshakeReq(const netlib::ISessionPtr& spGameSession, const ServerPacket::ServerHandshakeReq& msg)
{
    InternalSessionMeta* pMeta = getInternalSessionMeta(spGameSession);
    if (!pMeta)
        return;

    int32 gameServerId = msg.server_id();
    ServerType gameServerType = static_cast<ServerType>(msg.server_type());
    if (ServerType::Game != gameServerType)
    {
        LOG_WRITE(LogLevel::Error, std::format("game server handshake invalid server type. gameServerId={}, serverType={}", gameServerId, msg.server_type()));
        return;
    }

    if (pMeta->handshakeDone)
    {
        LOG_WRITE(LogLevel::Error, std::format("duplicated game server handshake request. gameServerId={}", gameServerId));
        return;
    }

    pMeta->handshakeDone = true;
    pMeta->isConnector = false;
    pMeta->peerServerId = gameServerId;
    pMeta->peerServerType = gameServerType;

    {
        std::lock_guard<std::mutex> lock(m_gameServerSelectionMutex);
        m_safeGameServerSessions.Insert(gameServerId, spGameSession);
    }

    // 게임서버에 Handshake Res 전송
    ServerPacket::ServerHandshakeRes res;
    res.set_success(true);
    res.set_server_id(m_serverId);
    netlib::PacketPtr spPacket = SerializePacket(Common::SERVER_PACKET_ID_SERVER_HANDSHAKE_RES, res);
    spGameSession->Send(spPacket);

    LOG_WRITE(LogLevel::Info, std::format("game server handshake complete. gameServerId={}", gameServerId));
}

void GatewayServer::handleUserMoveToGameServer(const netlib::ISessionPtr& /*spGameSession*/, const ServerPacket::UserMoveToGameServerReq& msg)
{
    int64 accountId             = msg.account_id();
    int32 targetGameServerId = msg.target_game_server_id();
    int32 targetStageId      = msg.target_stage_id();

    GatewayUserPtr spUser;
    if (!m_safeUsers.Find(accountId, spUser))
    {
        GetMetricsRegistry().Inc(serverbase::CounterMetric::Gateway_RerouteFailure);
        return;
    }

    netlib::ISessionPtr spTargetSession;
    if (!m_safeGameServerSessions.Find(targetGameServerId, spTargetSession))
    {
        GetMetricsRegistry().Inc(serverbase::CounterMetric::Gateway_RerouteFailure);
        LOG_WRITE(LogLevel::Warn, std::format("target game server not found. targetGameServerId={}, accountId={}", targetGameServerId, accountId));

        ServerPacket::UserMoveToGameServerFailNtf failNtf;
        failNtf.set_account_id(accountId);
        failNtf.set_reason("Target game server not found");

        auto spFailPacket = SerializePacket(Common::SERVER_PACKET_ID_USER_MOVE_TO_GAME_SERVER_FAIL_NTF, failNtf);
        if (spFailPacket)
            sendToGameServer(spUser->gameServerId, spFailPacket);
        return;
    }

    spUser->gameServerId = targetGameServerId;
    m_safeUsers.Insert(accountId, spUser);

    SessionMetaInfo* pMeta = getSessionMeta(spUser->spClientSession);
    if (pMeta)
        pMeta->routedGameServerId = targetGameServerId;

    upsertPrevGameServer(accountId, targetGameServerId);

    LOG_WRITE(LogLevel::Info, std::format("user rerouted. accountId={} -> gameServerId={}", accountId, targetGameServerId));

    ServerPacket::GatewayUserRerouteNtf rerouteNtf;
    rerouteNtf.set_account_id(accountId);
    rerouteNtf.set_gateway_id(GetServerId());
    rerouteNtf.set_target_stage_id(targetStageId);
    rerouteNtf.set_client_ip(spUser->clientIp);
    // 크로스서버 이동에 필요한 캐릭터/목적지 정보를 그대로 전달 (게이트웨이는 라우팅만 하고 내용은 해석하지 않음).
    rerouteNtf.set_character_id(msg.character_id());
    rerouteNtf.set_target_stage_data_key(msg.target_stage_data_key());
    rerouteNtf.set_position_type(msg.position_type());

    auto spReroutePacket = SerializePacket(Common::SERVER_PACKET_ID_USER_REROUTE_NTF, rerouteNtf);
    if (spReroutePacket)
    {
        spTargetSession->Send(spReroutePacket);
        GetMetricsRegistry().Inc(serverbase::CounterMetric::Gateway_RerouteSuccess);
    }
    else
    {
        GetMetricsRegistry().Inc(serverbase::CounterMetric::Gateway_RerouteFailure);
    }
}

// 게임서버의 netdelay 치트에 의한 클라 연결 지연 설정 (개발용)
void GatewayServer::handleSetClientLatency(const netlib::ISessionPtr& /*spGameSession*/, const ServerPacket::SetClientLatencyReq& msg)
{
    const int64 accountId = msg.account_id();

    GatewayUserPtr spUser;
    if (!m_safeUsers.Find(accountId, spUser) || !spUser->spClientSession)
    {
        LOG_WRITE(LogLevel::Warn, std::format("SetClientLatency: user not found. accountId={}", accountId));
        return;
    }

    spUser->spClientSession->SetSimulatedDelay(msg.recv_delay_ms(), msg.send_delay_ms());
    LOG_WRITE(LogLevel::Info, std::format("client latency set. accountId={} recvMs={} sendMs={}",
        accountId, msg.recv_delay_ms(), msg.send_delay_ms()));
}


// 인증토큰 저장
void GatewayServer::storeAuthToken(int64 accountId, uint64 authToken, int64 expireTimeMs)
{
    m_safeAuthTokens.Insert(accountId, { authToken, expireTimeMs });
}

// 인증토큰 소모
bool GatewayServer::consumeAuthToken(int64 accountId, uint64 authToken)
{
    int64 nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    AuthTokenEntry entry;
    if (!m_safeAuthTokens.EraseAndGet(accountId, entry))
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
void GatewayServer::upsertPrevGameServer(int64 accountId, int32 gameServerId)
{
    auto expireTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(k_prevGameServerTtlMs);
    m_safePrevGameServer.Insert(accountId, { gameServerId, expireTime });
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

std::optional<ServerInfo> GatewayServer::selectGameServer(int64 accountId)
{
    std::lock_guard<std::mutex> lock(m_gameServerSelectionMutex);

    if (m_safeGameServerInfos.Empty())
        return std::nullopt;

    auto isAvailable = [this](const ServerInfo& info)
    {
        return info.status == ServerStatus::Running && m_safeGameServerSessions.Contains(info.serverId);
    };

    // 이전 접속 게임서버 우선 선택 (5분 TTL)
    PrevGameServerEntry prevEntry;
    if (m_safePrevGameServer.Find(accountId, prevEntry))
    {
        if (std::chrono::steady_clock::now() < prevEntry.expireTime)
        {
            ServerInfo prevInfo;
            if (m_safeGameServerInfos.Find(prevEntry.gameServerId, prevInfo))
            {
                if (isAvailable(prevInfo))
                {
                    ++m_gameServerPendingAssignments[prevInfo.serverId];
                    return prevInfo;
                }
            }
        }
    }

    // Registry 접속자 수와 아직 스냅샷에 반영되지 않은 선택 수를 합산한다.
    int64 bestExpectedUserCount = std::numeric_limits<int64>::max();
    std::vector<ServerInfo> bestGameServers;
    m_safeGameServerInfos.ForEach([&](const int32&, const ServerInfo& info)
    {
        if (!isAvailable(info))
            return;

        int64 expectedUserCount = static_cast<int64>(info.userCount) + m_gameServerPendingAssignments[info.serverId];
        if (expectedUserCount < bestExpectedUserCount)
        {
            bestExpectedUserCount = expectedUserCount;
            bestGameServers.clear();
            bestGameServers.push_back(info);
        }
        else if (expectedUserCount == bestExpectedUserCount)
        {
            bestGameServers.push_back(info);
        }
    });

    if (bestGameServers.empty())
        return std::nullopt;

    size_t selectedIndex = static_cast<size_t>(m_gameServerRoundRobinCursor % bestGameServers.size());
    ++m_gameServerRoundRobinCursor;

    ServerInfo selected = bestGameServers[selectedIndex];
    ++m_gameServerPendingAssignments[selected.serverId];
    return selected;
}


// 게임서버로 서버간 패킷 전달
bool GatewayServer::sendToGameServer(int32 gameServerId, const netlib::PacketPtr& spPacket)
{
    netlib::ISessionPtr spSession;
    if (!m_safeGameServerSessions.Find(gameServerId, spSession))
    {
        LOG_WRITE(LogLevel::Warn, std::format("no session for gameServerId={}", gameServerId));
        return false;
    }

    spSession->Send(spPacket);
    return true;
}

void GatewayServer::forceDisconnectUser(int64 accountId, const std::string& reason)
{
    GatewayUserPtr spUser;
    if (!m_safeUsers.Find(accountId, spUser) || !spUser->spClientSession)
        return;

    LOG_WRITE(LogLevel::Info, std::format("force disconnecting accountId={}, reason={}", accountId, reason));

    GamePacket::ForceDisconnectNtf ntf;
    ntf.set_reason_code(GamePacket::FORCE_DISCONNECT_REASON_SERVER_SHUTDOWN);
    ntf.set_message(reason);

    auto spPacket = SerializePacket(Common::GAME_PACKET_ID_FORCE_DISCONNECT_NTF, ntf);
    if (spPacket)
        spUser->spClientSession->Send(spPacket);

    spUser->spClientSession->Disconnect();
}
