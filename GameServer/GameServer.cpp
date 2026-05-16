#include "pch.h"
#include "GameServer.h"

bool GameServer::OnInitialize()
{
    // ── 내부서버용 패킷 디스패처 ────────────────────────────────
    m_internalPacketDispatcher.SetUnknownPacketHandler([](const netlib::ISessionPtr& spSession, const netlib::PacketPtr& spPacket)
    {
        LOG_WRITE(LogLevel::Warn, std::format("GameServer: unknown internal packetId={} sessionId={}",
            spPacket->GetHeader()->type, spSession->GetId()));
    });

    // ── 내부 서버 네트워크 이벤트 핸들러 등록 ───────────────────
    m_internalListenEventHandler.onAccept     = [this](const netlib::ISessionPtr& spSession) { return onInternalAccept(spSession); };
    m_internalListenEventHandler.onRecv       = [this](const netlib::ISessionPtr& spSession, const netlib::PacketPtr& spPacket)
    {
        m_internalPacketDispatcher.Dispatch(spSession, spPacket);
    };
    m_internalListenEventHandler.onDisconnect = [this](const netlib::ISessionPtr& spSession) { onInternalDisconnect(spSession); };

    // ── 게이트웨이서버 패킷 디스패처 ────────────────────────────
    // 게이트웨이가 게임서버로 보내는 패킷(UserEnterNtf 등) 핸들러는 다음 단계에서 추가
    m_gatewayDispatcher.SetUnknownPacketHandler([](const netlib::ISessionPtr& spSession, const netlib::PacketPtr& spPacket)
    {
        LOG_WRITE(LogLevel::Warn, std::format("GameServer: unknown gateway packetId={} sessionId={}",
            spPacket->GetHeader()->type, spSession->GetId()));
    });

    // ── 게이트웨이서버 네트워크 이벤트 핸들러 등록 ──────────────
    m_gatewayEventHandler.onConnect    = [this](const netlib::ISessionPtr& spSession) { onGatewayConnect(spSession); };
    m_gatewayEventHandler.onRecv       = [this](const netlib::ISessionPtr& spSession, const netlib::PacketPtr& spPacket)
    {
        m_gatewayDispatcher.Dispatch(spSession, spPacket);
    };
    m_gatewayEventHandler.onDisconnect = [this](const netlib::ISessionPtr& spSession) { onGatewayDisconnect(spSession); };

    // ── 오픈필드 생성 및 컨텐츠 스레드 0번에 배정 ──────────────
    if (GetContentsThreadCount() <= k_openFieldThreadIndex)
    {
        LOG_WRITE(LogLevel::Error, std::format("GameServer::OnInitialize - not enough contents threads. need at least {} threads.",
            k_openFieldThreadIndex + 1));
        return false;
    }

    m_spOpenField = std::make_shared<OpenField>(k_openFieldStageId);
    AssignContents(k_openFieldThreadIndex, m_spOpenField);

    LOG_WRITE(LogLevel::Info, std::format("GameServer: OpenField created. stageId={} assignedThreadIndex={}",
        k_openFieldStageId, k_openFieldThreadIndex));

    LOG_WRITE(LogLevel::Info, std::format("GameServer::OnInitialize complete. serverId={}", GetServerId()));
    return true;
}

// 레지스트리 서버에서 다른 서버 정보를 받음
// 게이트웨이서버 정보가 갱신되면 connect/disconnect 처리
void GameServer::OnServerInfoUpdated(const ServerInfo& info)
{
    if (info.serverType != ServerType::Gateway)
        return;

    // 게이트웨이 정보 캐시 갱신
    if (info.status == ServerStatus::Disconnected)
        m_safeGatewayInfos.Erase(info.serverId);
    else
        m_safeGatewayInfos.Insert(info.serverId, info);

    // 상태에 따라 connect/disconnect
    // 종료대기(ShuttingDown) 상태인 게이트웨이로도 일단은 연결을 유지한다.
    // (이미 접속해 있는 유저들의 트래픽을 처리해야 하기 때문)
    if (info.status == ServerStatus::Running || info.status == ServerStatus::ShuttingDown)
    {
        connectToGateway(info.serverId, info.ip, info.internalPort);
    }
    else if (info.status == ServerStatus::Disconnected)
    {
        disconnectFromGateway(info.serverId);
    }
}

void GameServer::OnBeforeShutdown()
{
    LOG_WRITE(LogLevel::Info, "GameServer::OnBeforeShutdown");

    // 모든 게이트웨이서버 연결 끊기
    std::vector<int32> gatewayIds = m_safeGatewayClients.CollectKeys(
        [](const int32&, const netlib::NetClientPtr&) { return true; });

    for (int32 gatewayId : gatewayIds)
        disconnectFromGateway(gatewayId);

    // 오픈필드를 컨텐츠 스레드에서 제거
    // RemoveContents 호출 시 컨텐츠 스레드가 다음 tick에 OnStop을 호출하고 제거한다.
    if (m_spOpenField)
    {
        RemoveContents(k_openFieldThreadIndex, m_spOpenField);
    }
}

void GameServer::OnShutdown()
{
    LOG_WRITE(LogLevel::Info, "GameServer::OnShutdown");
}

// 내부 서버 연결 수락 (채팅서버 등)
bool GameServer::onInternalAccept(const netlib::ISessionPtr& spSession)
{
    if (IsShuttingDown())
        return false;

    LOG_WRITE(LogLevel::Info, std::format("GameServer: internal server connected. sessionId={}", spSession->GetId()));
    return true;
}

// 내부 서버 연결 끊김
void GameServer::onInternalDisconnect(const netlib::ISessionPtr& spSession)
{
    LOG_WRITE(LogLevel::Info, std::format("GameServer: internal server disconnected. sessionId={}", spSession->GetId()));
}

// ──────────────────────────────────────────────────────────────
// 게이트웨이서버 연결
// ──────────────────────────────────────────────────────────────

// 게이트웨이서버에 connect 성공
// 핸드셰이크를 전송하여 게임서버 ID를 알린다.
void GameServer::onGatewayConnect(const netlib::ISessionPtr& spSession)
{
    // 세션에 빈 메타 정보를 부착한다. gatewayServerId는 handshake 전송 시 채운다.
    spSession->SetUserData(std::make_shared<GatewaySessionMetaInfo>());

    LOG_WRITE(LogLevel::Info, std::format("GameServer: gateway connected. sessionId={}", spSession->GetId()));

    // 핸드셰이크 전송
    sendGameServerHandshake(spSession);
}

// 게이트웨이서버 연결 끊김
void GameServer::onGatewayDisconnect(const netlib::ISessionPtr& spSession)
{
    GatewaySessionMetaInfo* pMeta = getGatewaySessionMeta(spSession);
    if (!pMeta || pMeta->gatewayServerId == 0)
    {
        LOG_WRITE(LogLevel::Warn, std::format("GameServer: gateway disconnected before handshake. sessionId={}", spSession->GetId()));
        return;
    }

    int32 gatewayId = pMeta->gatewayServerId;
    m_safeGatewaySessions.Erase(gatewayId);

    LOG_WRITE(LogLevel::Warn, std::format("GameServer: gateway disconnected. gatewayId={}", gatewayId));
}

// 게이트웨이서버에 connect
void GameServer::connectToGateway(int32 gatewayId, const std::string& ip, uint16 port)
{
    if (m_safeGatewayClients.Contains(gatewayId))
        return;

    netlib::NetClientPtr spClient = ConnectToServer(ip, port, m_gatewayEventHandler);
    if (spClient)
    {
        m_safeGatewayClients.Insert(gatewayId, spClient);
        LOG_WRITE(LogLevel::Info, std::format("GameServer: connecting to gateway {} {}:{}", gatewayId, ip, port));
    }
    else
    {
        LOG_WRITE(LogLevel::Warn, std::format("GameServer: failed to create NetClient to gateway {} {}:{}", gatewayId, ip, port));
    }
}

// 게이트웨이서버 연결 끊기
void GameServer::disconnectFromGateway(int32 gatewayId)
{
    netlib::NetClientPtr spClient;
    if (!m_safeGatewayClients.EraseAndGet(gatewayId, spClient))
        return;

    if (spClient)
        DisconnectToServer(spClient);

    // m_safeGatewaySessions 정리는 onGatewayDisconnect 콜백에서 자동 처리됨

    LOG_WRITE(LogLevel::Info, std::format("GameServer: disconnected from gateway {}", gatewayId));
}

// 게이트웨이로 GameServerHandshakeNtf 전송
// 핸드셰이크가 성공하면 게이트웨이는 이 세션을 게임서버로 인식한다.
void GameServer::sendGameServerHandshake(const netlib::ISessionPtr& spGatewaySession)
{
    // 이 세션이 어떤 게이트웨이의 넷클라이언트의 세션인지 조회한다.
    // m_safeGatewayClients에 등록되어 있는 NetClient들을 순회하며 세션이 일치하는 것을 찾는다.
    // 게이트웨이 개수는 최대 100개 (서버구조개요.md 기준)이므로 순회 비용은 미미하다.
    int32 gatewayId = 0;
    std::vector<int32> allGatewayIds = m_safeGatewayClients.CollectKeys(
        [](const int32&, const netlib::NetClientPtr&) { return true; });

    for (int32 candidateId : allGatewayIds)
    {
        netlib::NetClientPtr spClient;
        if (!m_safeGatewayClients.Find(candidateId, spClient) || !spClient)
            continue;

        if (spClient->GetSession() == spGatewaySession)
        {
            gatewayId = candidateId;
            break;
        }
    }

    if (gatewayId == 0)
    {
        LOG_WRITE(LogLevel::Warn, std::format("GameServer: cannot identify gatewayId for session. sessionId={}", spGatewaySession->GetId()));
        return;
    }

    // 세션 메타에 gatewayId 기록
    GatewaySessionMetaInfo* pMeta = getGatewaySessionMeta(spGatewaySession);
    if (pMeta)
        pMeta->gatewayServerId = gatewayId;

    // 세션 등록 (핸드셰이크 전송 이전에 등록. 이 이후 이 세션으로 도착하는 패킷이 곳 처리될 수 있도록)
    m_safeGatewaySessions.Insert(gatewayId, spGatewaySession);

    // 핸드셰이크 패킷 전송
    ServerPacket::GameServerHandshakeNtf ntf;
    ntf.set_server_id(GetServerId());

    auto spPacket = SerializePacket(Common::SERVER_PACKET_ID_GAME_SERVER_HANDSHAKE_NTF, ntf);
    if (!spPacket)
    {
        LOG_WRITE(LogLevel::Error, std::format("GameServer: failed to serialize GameServerHandshakeNtf. gatewayId={}", gatewayId));
        return;
    }

    spGatewaySession->Send(spPacket);

    LOG_WRITE(LogLevel::Info, std::format("GameServer: sent GameServerHandshakeNtf. myServerId={} gatewayId={}",
        GetServerId(), gatewayId));
}

// 세션에서 GatewaySessionMetaInfo를 꺼낸다.
GatewaySessionMetaInfo* GameServer::getGatewaySessionMeta(const netlib::ISessionPtr& spSession)
{
    return static_cast<GatewaySessionMetaInfo*>(spSession->GetUserData().get());
}
