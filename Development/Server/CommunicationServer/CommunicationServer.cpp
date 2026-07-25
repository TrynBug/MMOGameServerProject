#include "pch.h"
#include "CommunicationServer.h"

bool CommunicationServer::OnInitialize()
{
    if (IsMetricsEnabled())
    {
        auto& r = GetMetricsRegistry();
        // 연결/Registry 정보/presence는 OnMetricsCollect에서 갱신하는 현재 상태 Gauge다.
        // chat/whisper Counter는 요청 결과별 처리량과 실패 원인을, fanout recipients는 실제 전파 규모를 누적한다.
        bool registered = true;
        registered &= r.AddGauge(serverbase::GaugeMetric::Communication_GameServerConnections, "mmo_communication_game_server_connections", "Authenticated GameServer connections.");
        registered &= r.AddGauge(serverbase::GaugeMetric::Communication_KnownGameServers, "mmo_communication_known_game_servers", "GameServers currently known from Registry.");
        registered &= r.AddGauge(serverbase::GaugeMetric::Communication_PresenceUsers, "mmo_communication_presence_users", "Characters currently in chat presence.");
        registered &= r.AddCounter(serverbase::CounterMetric::Communication_ChatSuccess, "mmo_communication_chat_total", "Global chat request results.", { { "result", "success" } });
        registered &= r.AddCounter(serverbase::CounterMetric::Communication_ChatRejected, "mmo_communication_chat_total", "Global chat request results.", { { "result", "rejected" } });
        registered &= r.AddCounter(serverbase::CounterMetric::Communication_ChatFanoutRecipients, "mmo_communication_chat_fanout_recipients_total", "Cumulative GameServer recipients of global chat.");
        registered &= r.AddCounter(serverbase::CounterMetric::Communication_WhisperSuccess, "mmo_communication_whisper_total", "Whisper routing results.", { { "result", "success" } });
        registered &= r.AddCounter(serverbase::CounterMetric::Communication_WhisperInvalid, "mmo_communication_whisper_total", "Whisper routing results.", { { "result", "invalid" } });
        registered &= r.AddCounter(serverbase::CounterMetric::Communication_WhisperOffline, "mmo_communication_whisper_total", "Whisper routing results.", { { "result", "offline" } });
        registered &= r.AddCounter(serverbase::CounterMetric::Communication_WhisperUnavailable, "mmo_communication_whisper_total", "Whisper routing results.", { { "result", "unavailable" } });
        if (!registered)
            return false;
    }

    // handshake가 끝나기 전에는 이 응답만 허용한다.
    m_gameServerDispatcher.Register<ServerPacket::ServerHandshakeRes>(Common::SERVER_PACKET_ID_SERVER_HANDSHAKE_RES,
        [this](auto& spSession, auto& msg) { handleGameServerHandshakeRes(spSession, msg); });

    // handshake 완료 후 처리할 채팅 도메인 패킷.
    m_gameServerDispatcher.Register<ServerPacket::ChatBroadcastReq>(Common::SERVER_PACKET_ID_CHAT_BROADCAST_REQ,
        [this](auto& spSession, auto& msg) { handleGameServerChatBroadcastReq(spSession, msg); });
    m_gameServerDispatcher.Register<ServerPacket::ChatPresenceNtf>(Common::SERVER_PACKET_ID_CHAT_PRESENCE_NTF,
        [this](auto& spSession, auto& msg) { handleGameServerChatPresenceNtf(spSession, msg); });
    m_gameServerDispatcher.Register<ServerPacket::WhisperReq>(Common::SERVER_PACKET_ID_WHISPER_REQ,
        [this](auto& spSession, auto& msg) { handleGameServerWhisperReq(spSession, msg); });

    m_gameServerDispatcher.SetUnknownPacketHandler([](const netlib::ISessionPtr& spSession, const netlib::PacketPtr& spPacket)
    {
        LOG_WRITE(LogLevel::Warn, std::format("unknown game server packet. sessionId={} packetId={}", spSession->GetId(), spPacket->GetHeader()->type));
    });

    m_gameServerEventHandler.onConnect = [this](const netlib::ISessionPtr& spSession) { onGameServerConnect(spSession); };
    m_gameServerEventHandler.onRecv = [this](const netlib::ISessionPtr& spSession, const netlib::PacketPtr& spPacket) { onGameServerRecv(spSession, spPacket); };
    m_gameServerEventHandler.onDisconnect = [this](const netlib::ISessionPtr& spSession) { onGameServerDisconnect(spSession); };

    return true;
}

void CommunicationServer::OnMetricsCollect()
{
    // known GameServer와 handshake 완료 session을 분리해 Registry에는 보이지만 연결되지 않은 서버를 찾을 수 있다.
    // presence는 채팅 routing 대상 character 수이며 GameServer user 수와 완전히 같은 의미는 아니다.
    auto& registry = GetMetricsRegistry();
    registry.Set(serverbase::GaugeMetric::Communication_GameServerConnections, static_cast<double>(m_safeGameServerSessions.Size()));
    registry.Set(serverbase::GaugeMetric::Communication_KnownGameServers, static_cast<double>(m_safeGameServerInfos.Size()));
    registry.Set(serverbase::GaugeMetric::Communication_PresenceUsers, static_cast<double>(m_safeCharacterPresences.Size()));
}

void CommunicationServer::OnServerInfoUpdated(const ServerInfo& info)
{
    if (info.serverType != ServerType::Game)
        return;

    // 접속 여부와 무관하게 최신 Registry 정보를 보관한다.
    // handshake 응답의 serverId가 Registry에 등록된 GameServer인지 검증할 때 사용한다.
    m_safeGameServerInfos.Insert(info.serverId, info);
    if (info.status == ServerStatus::Running)
        connectToGameServer(info.serverId, info.privateIp, info.internalPort);
    else
        disconnectFromGameServer(info.serverId);
}

void CommunicationServer::OnBeforeShutdown()
{
    // NetClient를 먼저 끊어 자동 재접속을 멈춘 뒤, handshake 완료 세션을 비운다.
    const std::vector<int32> gameServerIds = m_safeGameServerClients.CollectKeys([](const int32&, const netlib::NetClientPtr&) { return true; });
    for (int32 gameServerId : gameServerIds)
        disconnectFromGameServer(gameServerId);

    m_safeGameServerSessions.Clear();
    m_safeCharacterPresences.Clear();
}

void CommunicationServer::connectToGameServer(int32 gameServerId, const std::string& ip, uint16 port)
{
    if (m_safeGameServerClients.Contains(gameServerId))
        return;

    netlib::NetClientPtr spClient = ConnectToServer(ip, port, m_gameServerEventHandler);
    if (!spClient)
    {
        LOG_WRITE(LogLevel::Warn, std::format("failed to create connection to GameServer. serverId={} endpoint={}:{}", gameServerId, ip, port));
        return;
    }

    m_safeGameServerClients.Insert(gameServerId, spClient);
    LOG_WRITE(LogLevel::Info, std::format("connecting to GameServer. serverId={} endpoint={}:{}", gameServerId, ip, port));
}

void CommunicationServer::disconnectFromGameServer(int32 gameServerId)
{
    netlib::NetClientPtr spClient;
    if (m_safeGameServerClients.EraseAndGet(gameServerId, spClient) && spClient)
        DisconnectToServer(spClient);

    m_safeGameServerSessions.Erase(gameServerId);
}

void CommunicationServer::onGameServerConnect(const netlib::ISessionPtr& spSession)
{
    // 0은 handshake 전 상태다. handshake 응답에서 실제 GameServer ID로 교체한다.
    spSession->SetUserData(std::make_shared<int32>(0));

    ServerPacket::ServerHandshakeReq req;
    req.set_server_type(static_cast<int32>(ServerType::Communication));
    req.set_server_id(GetServerId());

    netlib::PacketPtr spPacket = SerializePacket(Common::SERVER_PACKET_ID_SERVER_HANDSHAKE_REQ, req);
    if (!spPacket)
    {
        LOG_WRITE(LogLevel::Error, std::format("failed to serialize GameServer handshake request. sessionId={}", spSession->GetId()));
        spSession->Disconnect();
        return;
    }

    spSession->Send(spPacket);
}

void CommunicationServer::onGameServerRecv(const netlib::ISessionPtr& spSession, const netlib::PacketPtr& spPacket)
{
    int32* pGameServerId = static_cast<int32*>(spSession->GetUserData().get());
    if (!pGameServerId)
    {
        LOG_WRITE(LogLevel::Error, std::format("game server session metadata missing. sessionId={}", spSession->GetId()));
        spSession->Disconnect();
        return;
    }

    // 인증 전 세션에는 handshake 응답 외의 패킷을 허용하지 않는다.
    if (*pGameServerId == 0 && spPacket->GetHeader()->type != Common::SERVER_PACKET_ID_SERVER_HANDSHAKE_RES)
    {
        LOG_WRITE(LogLevel::Warn, std::format("game server packet before handshake. sessionId={} packetId={}", spSession->GetId(), spPacket->GetHeader()->type));
        spSession->Disconnect();
        return;
    }

    m_gameServerDispatcher.Dispatch(spSession, spPacket);
}

void CommunicationServer::onGameServerDisconnect(const netlib::ISessionPtr& spSession)
{
    int32* pGameServerId = static_cast<int32*>(spSession->GetUserData().get());
    if (pGameServerId && *pGameServerId != 0)
    {
        netlib::ISessionPtr spRegisteredSession;
        if (m_safeGameServerSessions.Find(*pGameServerId, spRegisteredSession) && spRegisteredSession == spSession)
            m_safeGameServerSessions.Erase(*pGameServerId);

        // 끊어진 GameServer의 캐릭터는 더 이상 귓속말 대상이 아니다.
        const std::vector<std::string> characterNames = m_safeCharacterPresences.CollectKeys(
            [gameServerId = *pGameServerId](const std::string&, const CharacterChatPresence& presence) { return presence.gameServerId == gameServerId; });
        m_safeCharacterPresences.Erase(characterNames);
    }
}

void CommunicationServer::handleGameServerHandshakeRes(const netlib::ISessionPtr& spSession, const ServerPacket::ServerHandshakeRes& msg)
{
    int32* pGameServerId = static_cast<int32*>(spSession->GetUserData().get());
    if (!pGameServerId || *pGameServerId != 0 || !msg.success())
    {
        LOG_WRITE(LogLevel::Warn, std::format("GameServer handshake rejected. sessionId={} error={}", spSession->GetId(), msg.error_msg()));
        spSession->Disconnect();
        return;
    }

    // 응답에 담긴 ID가 Registry에서 Running 상태인 GameServer인지 확인한다.
    // 연결 대상 IP만으로 신뢰하지 않아 잘못된 서버가 세션을 가로채는 것을 막는다.
    ServerInfo gameServerInfo;
    if (!m_safeGameServerInfos.Find(msg.server_id(), gameServerInfo) || gameServerInfo.status != ServerStatus::Running)
    {
        LOG_WRITE(LogLevel::Warn, std::format("GameServer handshake response has unregistered serverId={}", msg.server_id()));
        spSession->Disconnect();
        return;
    }

    *pGameServerId = msg.server_id();
    m_safeGameServerSessions.Insert(*pGameServerId, spSession);
    LOG_WRITE(LogLevel::Info, std::format("GameServer handshake complete. serverId={} sessionId={}", *pGameServerId, spSession->GetId()));
}

void CommunicationServer::handleGameServerChatBroadcastReq(const netlib::ISessionPtr& spSession, const ServerPacket::ChatBroadcastReq& msg)
{
    int32* pGameServerId = static_cast<int32*>(spSession->GetUserData().get());
    netlib::ISessionPtr spRegisteredSession;
    if (!pGameServerId || *pGameServerId == 0 || !m_safeGameServerSessions.Find(*pGameServerId, spRegisteredSession) || spRegisteredSession != spSession)
    {
        GetMetricsRegistry().Inc(serverbase::CounterMetric::Communication_ChatRejected);
        LOG_WRITE(LogLevel::Warn, std::format("GlobalChat request rejected: unregistered GameServer session. sessionId={}", spSession->GetId()));
        spSession->Disconnect();
        return;
    }

    constexpr size_t k_maxChatMessageBytes = 256;
    if (msg.message().empty() || msg.message().size() > k_maxChatMessageBytes)
    {
        GetMetricsRegistry().Inc(serverbase::CounterMetric::Communication_ChatRejected);
        LOG_WRITE(LogLevel::Warn, std::format("GlobalChat request rejected: invalid message size. gameServerId={} size={}",
            *pGameServerId, msg.message().size()));
        return;
    }

    ServerPacket::ChatBroadcastNtf ntf;
    ntf.set_sender_character_id(msg.sender_character_id());
    ntf.set_sender_name(msg.sender_name());
    ntf.set_message(msg.message());

    netlib::PacketPtr spPacket = SerializePacket(Common::SERVER_PACKET_ID_CHAT_BROADCAST_NTF, ntf);
    if (!spPacket)
    {
        GetMetricsRegistry().Inc(serverbase::CounterMetric::Communication_ChatRejected);
        LOG_WRITE(LogLevel::Error, "GlobalChat failed to serialize ChatBroadcastNtf.");
        return;
    }

    // 인증된 모든 GameServer에 같은 패킷을 보내 각 서버가 로컬 유저에게 전달한다.
    uint64 recipients = 0;
    m_safeGameServerSessions.ForEach([&spPacket, &recipients](const int32&, const netlib::ISessionPtr& spGameServerSession)
    {
        if (spGameServerSession)
        {
            spGameServerSession->Send(spPacket);
            ++recipients;
        }
    });
    GetMetricsRegistry().Inc(serverbase::CounterMetric::Communication_ChatSuccess);
    GetMetricsRegistry().Inc(serverbase::CounterMetric::Communication_ChatFanoutRecipients, recipients);
}

void CommunicationServer::handleGameServerChatPresenceNtf(const netlib::ISessionPtr& spSession, const ServerPacket::ChatPresenceNtf& msg)
{
    int32* pGameServerId = static_cast<int32*>(spSession->GetUserData().get());
    netlib::ISessionPtr spRegisteredSession;
    if (!pGameServerId || *pGameServerId == 0 || !m_safeGameServerSessions.Find(*pGameServerId, spRegisteredSession) || spRegisteredSession != spSession)
    {
        LOG_WRITE(LogLevel::Warn, std::format("ChatPresence rejected: unregistered GameServer session. sessionId={}", spSession->GetId()));
        spSession->Disconnect();
        return;
    }

    if (msg.character_id() <= 0 || msg.character_name().empty())
    {
        LOG_WRITE(LogLevel::Warn, std::format("ChatPresence rejected: invalid character. gameServerId={} characterId={} characterName='{}'",
            *pGameServerId, msg.character_id(), msg.character_name()));
        return;
    }

    // online은 이름으로 upsert하고, offline은 같은 캐릭터와 서버가 등록한 항목만 제거한다.
    if (msg.online())
    {
        m_safeCharacterPresences.Insert(msg.character_name(), CharacterChatPresence{ msg.character_id(), *pGameServerId });
        return;
    }

    CharacterChatPresence presence;
    if (m_safeCharacterPresences.Find(msg.character_name(), presence) && presence.characterId == msg.character_id() && presence.gameServerId == *pGameServerId)
        m_safeCharacterPresences.Erase(msg.character_name());
}

void CommunicationServer::handleGameServerWhisperReq(const netlib::ISessionPtr& spSession, const ServerPacket::WhisperReq& msg)
{
    int32* pGameServerId = static_cast<int32*>(spSession->GetUserData().get());
    netlib::ISessionPtr spRegisteredSession;
    if (!pGameServerId || *pGameServerId == 0 || !m_safeGameServerSessions.Find(*pGameServerId, spRegisteredSession) || spRegisteredSession != spSession)
    {
        GetMetricsRegistry().Inc(serverbase::CounterMetric::Communication_WhisperInvalid);
        LOG_WRITE(LogLevel::Warn, std::format("Whisper rejected: unregistered GameServer session. sessionId={}", spSession->GetId()));
        spSession->Disconnect();
        return;
    }

    constexpr size_t k_maxChatMessageBytes = 256;
    if (msg.sender_account_id() <= 0 || msg.sender_character_id() <= 0 || msg.target_name().empty() || msg.message().empty() || msg.message().size() > k_maxChatMessageBytes)
    {
        GetMetricsRegistry().Inc(serverbase::CounterMetric::Communication_WhisperInvalid);
        LOG_WRITE(LogLevel::Warn, std::format("Whisper rejected: invalid request. gameServerId={} senderAccountId={} senderCharacterId={} targetName='{}' size={}",
            *pGameServerId, msg.sender_account_id(), msg.sender_character_id(), msg.target_name(), msg.message().size()));
        if (msg.sender_account_id() > 0)
            sendWhisperRes(spSession, msg, false, "귓속말 요청이 올바르지 않습니다.");
        return;
    }

    // 캐릭터 이름으로 대상 GameServer와 Character ID를 찾는다.
    CharacterChatPresence targetPresence;
    if (!m_safeCharacterPresences.Find(msg.target_name(), targetPresence))
    {
        GetMetricsRegistry().Inc(serverbase::CounterMetric::Communication_WhisperOffline);
        LOG_WRITE(LogLevel::Info, std::format("Whisper target offline. senderCharacterId={} targetName='{}'",
            msg.sender_character_id(), msg.target_name()));
        sendWhisperRes(spSession, msg, false, "대상을 찾을 수 없거나 오프라인 상태입니다.");
        return;
    }

    netlib::ISessionPtr spTargetGameServerSession;
    if (!m_safeGameServerSessions.Find(targetPresence.gameServerId, spTargetGameServerSession) || !spTargetGameServerSession)
    {
        GetMetricsRegistry().Inc(serverbase::CounterMetric::Communication_WhisperUnavailable);
        LOG_WRITE(LogLevel::Warn, std::format("Whisper target GameServer unavailable. targetName='{}' gameServerId={}",
            msg.target_name(), targetPresence.gameServerId));
        sendWhisperRes(spSession, msg, false, "대상 서버에 연결할 수 없습니다.");
        return;
    }

    // 대상 GameServer 한 곳으로 전달한 뒤 송신 GameServer에 성공 여부를 돌려준다.
    ServerPacket::WhisperNtf ntf;
    ntf.set_sender_character_id(msg.sender_character_id());
    ntf.set_sender_name(msg.sender_name());
    ntf.set_target_character_id(targetPresence.characterId);
    ntf.set_message(msg.message());

    netlib::PacketPtr spPacket = SerializePacket(Common::SERVER_PACKET_ID_WHISPER_NTF, ntf);
    if (!spPacket)
    {
        GetMetricsRegistry().Inc(serverbase::CounterMetric::Communication_WhisperUnavailable);
        LOG_WRITE(LogLevel::Error, "Whisper failed to serialize WhisperNtf.");
        sendWhisperRes(spSession, msg, false, "귓속말 전달에 실패했습니다.");
        return;
    }

    spTargetGameServerSession->Send(spPacket);
    sendWhisperRes(spSession, msg, true, "");
    GetMetricsRegistry().Inc(serverbase::CounterMetric::Communication_WhisperSuccess);
}

void CommunicationServer::sendWhisperRes(const netlib::ISessionPtr& spSession, const ServerPacket::WhisperReq& req, bool success, const std::string& errorMsg)
{
    ServerPacket::WhisperRes res;
    res.set_sender_account_id(req.sender_account_id());
    res.set_sender_name(req.sender_name());
    res.set_target_name(req.target_name());
    res.set_message(req.message());
    res.set_success(success);
    res.set_error_msg(errorMsg);

    netlib::PacketPtr spPacket = SerializePacket(Common::SERVER_PACKET_ID_WHISPER_RES, res);
    if (!spPacket)
    {
        LOG_WRITE(LogLevel::Error, std::format("Whisper failed to serialize WhisperRes. senderAccountId={}", req.sender_account_id()));
        return;
    }

    spSession->Send(spPacket);
}
