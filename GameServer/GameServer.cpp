#include "pch.h"
#include "GameServer.h"

namespace
{
    constexpr const char* k_gameDBPath = "GameDB.db";
}

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
    m_gatewayDispatcher.Register<ServerPacket::GatewayUserEnterNtf>(Common::SERVER_PACKET_ID_USER_ENTER_NTF,
        [this](auto& spSession, auto& msg) { handleGatewayUserEnter(spSession, msg); });

    m_gatewayDispatcher.Register<ServerPacket::GatewayUserDisconnectNtf>(Common::SERVER_PACKET_ID_USER_DISCONNECT_NTF,
        [this](auto& spSession, auto& msg) { handleGatewayUserDisconnect(spSession, msg); });

    m_gatewayDispatcher.SetUnknownPacketHandler([](const netlib::ISessionPtr& spSession, const netlib::PacketPtr& spPacket)
    {
        LOG_WRITE(LogLevel::Warn, std::format("GameServer: unknown gateway packetId={} sessionId={}",
            spPacket->GetHeader()->type, spSession->GetId()));
    });

    // ── 게이트웨이서버 네트워크 이벤트 핸들러 등록 ──────────────
    m_gatewayEventHandler.onConnect    = [this](const netlib::ISessionPtr& spSession) { onGatewayConnect(spSession); };
    m_gatewayEventHandler.onRecv       = [this](const netlib::ISessionPtr& spSession, const netlib::PacketPtr& spPacket)
    {
        // 사이드카가 있으면 클라이언트가 보낸 패킷이 게이트웨이를 거쳐 온 것.
        // (게이트웨이가 원본 클라 패킷에 userId 사이드카를 더해서 전송함)
        // 그외의 패킷은 서버간 통신 패킷이므로 dispatcher로 처리.
        if (spPacket->HasSidecar())
        {
            handleRelayedClientPacket(spPacket);
        }
        else
        {
            m_gatewayDispatcher.Dispatch(spSession, spPacket);
        }
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

    // ── GameDB 열기 ────────────────────────────────────────────
    if (!m_dbQueue.Open(k_gameDBPath, 1))
    {
        LOG_WRITE(LogLevel::Error, std::format("GameServer::OnInitialize - failed to open GameDB at {}", k_gameDBPath));
        return false;
    }

    initGameDB();

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

    // 모든 유저 정리. Stage에서도 제거되고, 글로벌 맵에서도 제거된다.
    // 향후 단계에서 DB 저장 등이 추가될 예정.
    m_safeUsers.Clear();

    // 모든 게이트웨이서버 연결 끊기
    std::vector<int32> gatewayIds = m_safeGatewayClients.CollectKeys(
        [](const int32&, const netlib::NetClientPtr&) { return true; });

    for (int32 gatewayId : gatewayIds)
        disconnectFromGateway(gatewayId);

    // 오픈필드를 컨텐츠 스레드에서 제거
    if (m_spOpenField)
    {
        RemoveContents(k_openFieldThreadIndex, m_spOpenField);
    }

    // GameDB 닫기 (큐에 남은 요청 처리 후 종료)
    m_dbQueue.Close();
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

void GameServer::onGatewayConnect(const netlib::ISessionPtr& spSession)
{
    // 세션에 빈 메타 정보를 부착한다. gatewayServerId는 handshake 전송 시 채운다.
    spSession->SetUserData(std::make_shared<GatewaySessionMetaInfo>());

    LOG_WRITE(LogLevel::Info, std::format("GameServer: gateway connected. sessionId={}", spSession->GetId()));

    // 핸드셰이크 전송
    sendGameServerHandshake(spSession);
}

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

void GameServer::disconnectFromGateway(int32 gatewayId)
{
    netlib::NetClientPtr spClient;
    if (!m_safeGatewayClients.EraseAndGet(gatewayId, spClient))
        return;

    if (spClient)
        DisconnectToServer(spClient);

    LOG_WRITE(LogLevel::Info, std::format("GameServer: disconnected from gateway {}", gatewayId));
}

void GameServer::sendGameServerHandshake(const netlib::ISessionPtr& spGatewaySession)
{
    // 이 세션이 어떤 게이트웨이의 넷클라이언트의 세션인지 조회한다.
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

GatewaySessionMetaInfo* GameServer::getGatewaySessionMeta(const netlib::ISessionPtr& spSession)
{
    return static_cast<GatewaySessionMetaInfo*>(spSession->GetUserData().get());
}

// ──────────────────────────────────────────────────────────────
// 게이트웨이로부터 받은 유저 관련 패킷 처리
// ──────────────────────────────────────────────────────────────

// 게이트웨이로부터 GatewayUserEnterNtf 수신 → 코루틴
// 1) DB에서 캐릭터 JSON 조회
// 2) 없으면 기본값 캐릭터 생성 후 DB에 INSERT
// 3) 유저 객체 생성, 글로벌 맵 등록, 오픈필드에 입장 메시지 push
// 4) GameEnterNtf 응답
db::DBTask<void> GameServer::handleGatewayUserEnter(netlib::ISessionPtr /*spSession*/, ServerPacket::GatewayUserEnterNtf msg)
{
    const int64 userId    = msg.user_id();
    const int32 gatewayId = msg.gateway_id();
    const std::string clientIp = msg.client_ip();

    LOG_WRITE(LogLevel::Info, std::format("GameServer: GatewayUserEnterNtf received. userId={} gatewayId={} clientIp={}",
        userId, gatewayId, clientIp));

    // 이미 입장한 유저인지 확인
    if (m_safeUsers.Contains(userId))
    {
        LOG_WRITE(LogLevel::Warn, std::format("GameServer: user already exists. userId={}", userId));
        co_return;
    }

    // ── 1) DB에서 캐릭터 조회 ────────────────────────────────────
    db::DBResult result = co_await m_dbQueue.ExecuteAsync(
        "SELECT data FROM Characters WHERE user_id = ? LIMIT 1",
        { userId },
        GetCoroutineResumeExecutor()
    );

    if (!result.success)
    {
        LOG_WRITE(LogLevel::Error, std::format("GameServer: DB select failed. userId={} err={}", userId, result.errorMsg));
        co_return;
    }

    DataStructures::Character character;
    bool needInsert = false;

    if (result.IsEmpty())
    {
        // ── 2) 캐릭터 없음 → 기본값으로 생성 ────────────────────
        character.set_user_id(userId);
        character.set_name(std::format("User_{}", userId));
        character.set_level(1);
        character.set_exp(0);
        character.set_hp(100);
        character.set_max_hp(100);
        character.set_mp(50);
        character.set_max_mp(50);
        character.set_last_stage_id(static_cast<int32>(k_openFieldStageId));
        character.set_pos_x(0.0f);
        character.set_pos_y(0.0f);
        character.set_pos_z(0.0f);
        character.set_dir_y(0.0f);
        needInsert = true;

        LOG_WRITE(LogLevel::Info, std::format("GameServer: character not found in DB. created default. userId={}", userId));
    }
    else
    {
        // 캐릭터 데이터 JSON을 protobuf 메시지로 역직렬화
        const std::string dataJson = result.GetString(0, "data");
        if (!packet::ProtoJsonSerializer::FromJson(dataJson, character))
        {
            LOG_WRITE(LogLevel::Error, std::format("GameServer: failed to parse character JSON. userId={}", userId));
            co_return;
        }

        LOG_WRITE(LogLevel::Info, std::format("GameServer: character loaded from DB. userId={} name={} level={}",
            userId, character.name(), character.level()));
    }

    // ── 3) 캐릭터 신규 생성 시 DB에 INSERT ──────────────────────
    if (needInsert)
    {
        std::string dataJson;
        if (!packet::ProtoJsonSerializer::ToJson(character, dataJson))
        {
            LOG_WRITE(LogLevel::Error, std::format("GameServer: failed to serialize character to JSON. userId={}", userId));
            co_return;
        }

        db::DBResult insertResult = co_await m_dbQueue.ExecuteAsync(
            "INSERT OR IGNORE INTO Characters (user_id, data) VALUES (?, ?)",
            { userId, dataJson },
            GetCoroutineResumeExecutor()
        );

        if (!insertResult.success)
        {
            LOG_WRITE(LogLevel::Error, std::format("GameServer: DB insert failed. userId={} err={}", userId, insertResult.errorMsg));
            co_return;
        }
    }

    // ── 4) 유저 객체 생성 및 글로벌 맵 등록 ──────────────────────
    UserPtr spUser = std::make_shared<User>(userId, gatewayId, clientIp);
    m_safeUsers.Insert(userId, spUser);

    // 오픈필드에 입장 메시지 push (다음 tick에서 Stage가 처리)
    if (m_spOpenField)
    {
        m_spOpenField->EnqueueMessage(StageMsg_UserEnter{spUser});
    }
    else
    {
        LOG_WRITE(LogLevel::Error, std::format("GameServer: open field is null. userId={}", userId));
        co_return;
    }

    // ── 5) GameEnterNtf 응답 (게이트웨이를 통해 클라에게) ────────
    sendGameEnterNtf(userId, character);
}

void GameServer::sendGameEnterNtf(int64 userId, const DataStructures::Character& character)
{
    GamePacket::GameEnterNtf ntf;
    *ntf.mutable_character() = character;
    ntf.set_stage_id(static_cast<int32>(k_openFieldStageId));

    sendPacketToUser(userId, Common::GAME_PACKET_ID_GAME_ENTER_NTF, ntf);
}

// 게이트웨이로부터 GatewayUserDisconnectNtf 수신
// → 글로벌 맵에서 제거, 현재 Stage에 퇴장 메시지 push
void GameServer::handleGatewayUserDisconnect(const netlib::ISessionPtr& /*spSession*/, const ServerPacket::GatewayUserDisconnectNtf& msg)
{
    const int64 userId = msg.user_id();

    LOG_WRITE(LogLevel::Info, std::format("GameServer: GatewayUserDisconnectNtf received. userId={}", userId));

    UserPtr spUser;
    if (!m_safeUsers.EraseAndGet(userId, spUser) || !spUser)
    {
        LOG_WRITE(LogLevel::Warn, std::format("GameServer: user not found on disconnect. userId={}", userId));
        return;
    }

    // 현재 Stage에 퇴장 메시지 push
    // 4단계에서는 오픈필드만 존재하므로 오픈필드에 push.
    // 향후 다중 Stage 구현 시 spUser->GetCurrentStageId()로 정확한 Stage를 찾아 push해야 함.
    if (m_spOpenField)
    {
        m_spOpenField->EnqueueMessage(StageMsg_UserLeave{userId});
    }
}

// ──────────────────────────────────────────────────────────────
// GameDB 초기화
// ──────────────────────────────────────────────────────────────

// 게이트웨이로부터 받은 클라 패킷 (사이드카 있음) 처리
// → 사이드카에서 userId 추출 → 해당 유저의 패킷 큐에 push
// 유저의 Stage Update 시 OnUserPacket으로 처리됨.
void GameServer::handleRelayedClientPacket(const netlib::PacketPtr& spPacket)
{
    if (!spPacket->HasSidecar())
        return;

    // 사이드카 크기 검증 (게이트웨이는 int64 userId 8바이트를 붙이기로 약속)
    if (spPacket->GetSidecarSize() != sizeof(int64))
    {
        LOG_WRITE(LogLevel::Warn, std::format("GameServer: unexpected sidecar size. expected={} actual={} packetType={}",
            sizeof(int64), spPacket->GetSidecarSize(), spPacket->GetHeader()->type));
        return;
    }

    // userId 추출
    int64 userId = 0;
    std::memcpy(&userId, spPacket->GetSidecarData(), sizeof(int64));

    // 유저 조회
    UserPtr spUser;
    if (!m_safeUsers.Find(userId, spUser) || !spUser)
    {
        LOG_WRITE(LogLevel::Warn, std::format("GameServer: relayed client packet for unknown user. userId={} packetType={}",
            userId, spPacket->GetHeader()->type));
        return;
    }

    // 유저 패킷 큐에 push (Stage 스레드가 다음 tick에서 drain해서 처리)
    spUser->EnqueuePacket(spPacket);
}

void GameServer::initGameDB()
{
    // 동기 DBConnection으로 스키마 점검 (서버 시작 시 1회)
    // 정식 스키마 생성은 Common/init_gamedb.bat으로 수동 처리하지만,
    // 안전을 위해 CREATE TABLE IF NOT EXISTS도 여기서 한 번 실행한다.
    db::DBConnection conn;
    if (!conn.Open(k_gameDBPath))
    {
        LOG_WRITE(LogLevel::Error, std::format("GameServer::initGameDB - failed to open DB at {}", k_gameDBPath));
        return;
    }

    auto res = conn.Execute(R"(
        CREATE TABLE IF NOT EXISTS Characters (
            user_id       INTEGER PRIMARY KEY,
            data          TEXT    NOT NULL,
            last_updated  INTEGER NOT NULL DEFAULT (strftime('%s','now') * 1000)
        )
    )");

    if (!res.success)
    {
        LOG_WRITE(LogLevel::Error, std::format("GameServer::initGameDB - schema execute failed: {}", res.errorMsg));
        return;
    }

    LOG_WRITE(LogLevel::Info, "GameServer::initGameDB - schema ready");
}
