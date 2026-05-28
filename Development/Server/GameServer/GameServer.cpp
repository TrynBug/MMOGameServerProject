#include "pch.h"
#include "GameServer.h"
#include "Character.h"   // handleClientCharacterSelect에서 Character 객체 생성

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

    m_gatewayDispatcher.Register<ServerPacket::ServerHandshakeRes>(Common::SERVER_PACKET_ID_SERVER_HANDSHAKE_RES,
        [this](auto& spSession, auto& msg) { handleGatewayHandshakeRes(spSession, msg); });

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



    const std::filesystem::path currPath = std::filesystem::current_path();

    // NavMeshManager 초기화
    const std::filesystem::path navMeshDir = currPath.parent_path() / "Map" / "NavMesh";
    if (!m_navMeshManager.LoadAll(navMeshDir))
    {
        LOG_WRITE(LogLevel::Error, std::format("GameServer::OnInitialize - failed to initialize NavMeshManager. navMeshDir={}", navMeshDir.string()));
        return false;
    }

    // ── StageManager 초기화 + 고정 Stage 생성 ──────────────────
    // StageManager가 Stage 생성 + SetGameServer + AssignContents까지 처리.
    if (GetContentsThreadCount() <= 0)
    {
        LOG_WRITE(LogLevel::Error, "GameServer::OnInitialize - no contents threads available.");
        return false;
    }

    m_stageManager.Initialize(this, GetContentsThreadCount());

    if (!m_stageManager.CreateSystemStage(k_systemStageId))
    {
        LOG_WRITE(LogLevel::Error, std::format("GameServer::OnInitialize - failed to create SystemStage. stageId={}", k_systemStageId));
        return false;
    }

    if (!m_stageManager.CreateTown(k_townStageId))
    {
        LOG_WRITE(LogLevel::Error, std::format("GameServer::OnInitialize - failed to create Town. stageId={}", k_townStageId));
        return false;
    }

    // ── GameDB 열기 ────────────────────────────────────────────
    if (!m_dbQueue.Open(k_gameDBPath, 1))
    {
        LOG_WRITE(LogLevel::Error, std::format("GameServer::OnInitialize - failed to open GameDB at {}", k_gameDBPath));
        return false;
    }

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

    // Stage들을 컨텐츠 스레드에서 제거 + StageManager 비우기
    m_stageManager.Clear();

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
    spSession->SetUserData(std::make_shared<InternalSessionMeta>());

    LOG_WRITE(LogLevel::Info, std::format("GameServer: gateway connected. sessionId={}", spSession->GetId()));

    // 핸드셰이크 전송
    sendGameServerHandshakeReq(spSession);
}

void GameServer::onGatewayDisconnect(const netlib::ISessionPtr& spSession)
{
    // 세션 메타정보 peer 서버 ID 조회
    InternalSessionMeta* pMeta = getInternalSessionMeta(spSession);
    if (!pMeta || !pMeta->handshakeDone)
    {
        LOG_WRITE(LogLevel::Warn, std::format("GameServer: gateway disconnected before handshake. sessionId={}", spSession->GetId()));
        return;
    }

    int32 gatewayId = pMeta->peerServerId;
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

void GameServer::sendGameServerHandshakeReq(const netlib::ISessionPtr& spGatewaySession)
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
    InternalSessionMeta* pMeta = getInternalSessionMeta(spGatewaySession);
    pMeta->handshakeDone = false;
    pMeta->isConnector = true;
    pMeta->peerServerId = gatewayId;
    pMeta->peerServerType = ServerType::Gateway;

    // 세션 등록 (핸드셰이크 전송 이전에 등록. 이 이후 이 세션으로 도착하는 패킷이 곳 처리될 수 있도록)
    m_safeGatewaySessions.Insert(gatewayId, spGatewaySession);

    // 핸드셰이크 패킷 전송
    // 게이트웨이 서버에 Handshake Req 전송
    ServerPacket::ServerHandshakeReq req;
    req.set_server_type(static_cast<int32>(ServerType::Game));
    req.set_server_id(m_serverId);

    auto spPacket = SerializePacket(Common::SERVER_PACKET_ID_SERVER_HANDSHAKE_REQ, req);
    if (!spPacket)
    {
        LOG_WRITE(LogLevel::Error, std::format("GameServer: failed to serialize GameServerHandshakeNtf. gatewayId={}", gatewayId));
        return;
    }

    spGatewaySession->Send(spPacket);

    LOG_WRITE(LogLevel::Info, std::format("GameServer: sent GameServerHandshakeNtf. myServerId={} gatewayId={}",
        GetServerId(), gatewayId));
}

InternalSessionMeta* GameServer::getInternalSessionMeta(const netlib::ISessionPtr& spSession)
{
    return static_cast<InternalSessionMeta*>(spSession->GetUserData().get());
}

// ──────────────────────────────────────────────────────────────
// 게이트웨이로부터 받은 유저 관련 패킷 처리
// ──────────────────────────────────────────────────────────────

// 게이트웨이로부터 GatewayUserEnterNtf 수신 → 코루틴
// 1) DB에서 캐릭터 목록 조회 (없으면 빈 목록)
// 2) 유저 객체 생성 및 글로벌 맵 등록
// 3) SystemStage(캐릭터 선택창)에 입장 메시지 push
// 4) GameEnterNtf + CharacterListNtf 응답
db::DetachedCoTask GameServer::handleGatewayUserEnter(netlib::ISessionPtr /*spSession*/, ServerPacket::GatewayUserEnterNtf msg)
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
        "SELECT data FROM Characters WHERE user_id = ?",
        { userId },
        GetCoroutineResumeExecutor()
    );

    if (!result.success)
    {
        LOG_WRITE(LogLevel::Error, std::format("GameServer: DB select failed. userId={} err={}", userId, result.errorMsg));
        co_return;
    }

    // 조회된 모든 캐릭터를 protobuf 메시지로 역직렬화하여 목록에 적재.
    std::vector<DataStructures::Character> characters;
    characters.reserve(result.RowCount());
    for (int row = 0; row < result.RowCount(); ++row)
    {
        const std::string dataJson = result.GetString(row, "data");
        DataStructures::Character character;
        if (!packet::ProtoJsonSerializer::FromJson(dataJson, character))
        {
            LOG_WRITE(LogLevel::Error, std::format("GameServer: failed to parse character JSON. userId={} row={}", userId, row));
            continue;
        }
        characters.push_back(std::move(character));
    }

    LOG_WRITE(LogLevel::Info, std::format("GameServer: characters loaded from DB. userId={} count={}",
        userId, characters.size()));

    // ── 2) 유저 객체 생성 및 글로벌 맵 등록 ────────────────────
    UserPtr spUser = std::make_shared<User>(userId, gatewayId, clientIp);
    m_safeUsers.Insert(userId, spUser);

    // ── 3) SystemStage(캐릭터 선택창)에 입장 메시지 push ──────────
    // 캐릭터 선택이 끝나면 handleClientCharacterSelect에서 Town으로 이동시킨다.
    SystemStagePtr spSystemStage = m_stageManager.GetSystemStage();
    if (spSystemStage)
    {
        spSystemStage->EnqueueMessage(StageMsg_UserEnter{spUser, nullptr});
    }
    else
    {
        LOG_WRITE(LogLevel::Error, std::format("GameServer: system stage is null. userId={}", userId));
        co_return;
    }

    // ── 4) GameEnterNtf + CharacterListNtf 응답 (게이트웨이 통해 클라에게) ──
    sendGameEnterNtf(userId);
    sendCharacterListNtf(userId, characters);
}

void GameServer::sendGameEnterNtf(int64 userId)
{
    GamePacket::GameEnterNtf ntf;
    ntf.set_stage_id(k_systemStageId);

    sendPacketToUser(userId, Common::GAME_PACKET_ID_GAME_ENTER_NTF, ntf);
}

void GameServer::sendCharacterListNtf(int64 userId, const std::vector<DataStructures::Character>& characters)
{
    GamePacket::CharacterListNtf ntf;
    for (const auto& character : characters)
    {
        *ntf.add_characters() = character;
    }
    sendPacketToUser(userId, Common::GAME_PACKET_ID_CHARACTER_LIST_NTF, ntf);

    LOG_WRITE(LogLevel::Info, std::format("GameServer: CharacterListNtf sent. userId={} count={}",
        userId, characters.size()));
}

// 클라이언트 캐릭터 생성 요청 처리 → 코루틴
// 1) 명칭 검증 (빈 문자열만 거부, 상세 검증은 향후 추가)
// 2) ObjectId 발급 + 기본값 캐릭터 구성
// 3) DB INSERT
// 4) CharacterCreateRes 전송 (성공/실패)
db::DetachedCoTask GameServer::handleClientCharacterCreate(int64 userId, GamePacket::CharacterCreateReq req)
{
    LOG_WRITE(LogLevel::Info, std::format("GameServer: CharacterCreateReq received. userId={} name='{}' jobId={}",
        userId, req.name(), req.job_id()));

    // ── 1) 명칭 검증 ────────────────────────────────────────
    if (req.name().empty())
    {
        sendCharacterCreateRes(userId, EResultCode::Fail, "name is empty", nullptr);
        co_return;
    }

    // ── 2) 캐릭터 구성 ──────────────────────────────────────
    DataStructures::Character character;
    character.set_character_id(GenerateObjectId());
    character.set_owner_user_id(userId);
    character.set_name(req.name());
    character.set_job_id(req.job_id());
    character.set_level(1);
    character.set_exp(0);
    character.set_last_stage_id(k_townStageId);
    character.set_pos_x(0.0f);
    character.set_pos_y(0.0f);
    character.set_pos_z(0.0f);
    character.set_yaw(0.0f);

    // ── 3) DB INSERT ──────────────────────────────────────────
    std::string dataJson;
    if (!packet::ProtoJsonSerializer::ToJson(character, dataJson))
    {
        LOG_WRITE(LogLevel::Error, std::format("GameServer: failed to serialize character to JSON. userId={}", userId));
        sendCharacterCreateRes(userId, EResultCode::Fail, "server error: serialize", nullptr);
        co_return;
    }

    db::DBResult insertResult = co_await m_dbQueue.ExecuteAsync(
        "INSERT INTO Characters (user_id, character_id, data) VALUES (?, ?, ?)",
        { userId, character.character_id(), dataJson },
        GetCoroutineResumeExecutor()
    );

    if (!insertResult.success)
    {
        LOG_WRITE(LogLevel::Error, std::format("GameServer: CharacterCreate DB insert failed. userId={} err={}", userId, insertResult.errorMsg));
        sendCharacterCreateRes(userId, EResultCode::Fail, "server error: db insert", nullptr);
        co_return;
    }

    LOG_WRITE(LogLevel::Info, std::format("GameServer: character created. userId={} characterId={} name='{}'",
        userId, character.character_id(), character.name()));

    // ── 4) 성공 응답 ────────────────────────────────────────────
    sendCharacterCreateRes(userId, EResultCode::Success, "", &character);
}

void GameServer::sendCharacterCreateRes(int64 userId, EResultCode resultCode, const std::string& errorMsg, const DataStructures::Character* pNewCharacter)
{
    GamePacket::CharacterCreateRes res;
    res.set_result_code(static_cast<int32>(resultCode));
    res.set_error_msg(errorMsg);
    if (pNewCharacter)
    {
        *res.mutable_new_character() = *pNewCharacter;
    }
    sendPacketToUser(userId, Common::GAME_PACKET_ID_CHARACTER_CREATE_RES, res);
}

void GameServer::SendStageEnterNtf(int64 userId, int64 stageId, float myPosX, float myPosY, float myPosZ, float myYaw)
{
    // StageEnterNtf 먼저 스탯/HP를 보낸다.
    // 클라는 StageEnterNtf 를 받을 때면 이미 Game 씬 + LocalPlayer 스폰이 끝난 상태라
    // 스탯 핸들러가 대상 캐릭터를 찾을 수 있다. 또 최대치(StatUpdateNtf)가 현재HP(HpMpNtf)보다
    // 먼저 가야 클라에서 clamp 가 올바르므로 이 순서로 보낸다.
    UserPtr spUser;
    if (m_safeUsers.Find(userId, spUser) && spUser)
    {
        if (CharacterPtr spCharacter = spUser->GetCurrentCharacter())
        {
            SendStatUpdateNtf(userId, *spCharacter);
            SendHpMpNtf(userId, spCharacter->GetCurHp(), spCharacter->GetCurMp());
        }
    }

    GamePacket::StageEnterNtf ntf;
    ntf.set_stage_id(stageId);
    ntf.set_my_pos_x(myPosX);
    ntf.set_my_pos_y(myPosY);
    ntf.set_my_pos_z(myPosZ);
    ntf.set_my_yaw(myYaw);

    sendPacketToUser(userId, Common::GAME_PACKET_ID_STAGE_ENTER_NTF, ntf);

    LOG_WRITE(LogLevel::Info, std::format("GameServer: StageEnterNtf sent. userId={} stageId={} pos=({},{},{}) yaw={}",
        userId, stageId, myPosX, myPosY, myPosZ, myYaw));
}

void GameServer::SendStatUpdateNtf(int64 userId, const Character& character)
{
    GamePacket::StatUpdateNtf ntf;
    ntf.set_object_id(character.GetObjectId());

    // 0 이 아닌 스탯만 담는다.
    character.GetStat().ForEachNonZeroStat([&ntf](EStat stat, double value)
    {
        GamePacket::StatEntry* pEntry = ntf.add_entries();
        pEntry->set_stat(static_cast<int32>(stat));
        pEntry->set_value(value);
    });

    sendPacketToUser(userId, Common::GAME_PACKET_ID_STAT_UPDATE_NTF, ntf);

    LOG_WRITE(LogLevel::Info, std::format("GameServer: StatUpdateNtf sent. userId={} objectId={} count={}",
        userId, character.GetObjectId(), ntf.entries_size()));
}

void GameServer::SendHpMpNtf(int64 userId, double curHp, double curMp)
{
    GamePacket::HpMpNtf ntf;
    // objectId 는 현재 본인에게만 보내므로 userId 와 동일한 캐릭터 objectId 를 쓴다.
    // (현재 character_id == objectId == userId 체계. 향후 구분되면 명시 전달로 변경.)
    UserPtr spUser;
    int64 objectId = userId;
    if (m_safeUsers.Find(userId, spUser) && spUser)
    {
        if (CharacterPtr spCharacter = spUser->GetCurrentCharacter())
            objectId = spCharacter->GetObjectId();
    }

    ntf.set_object_id(objectId);
    ntf.set_cur_hp(curHp);
    ntf.set_cur_mp(curMp);

    sendPacketToUser(userId, Common::GAME_PACKET_ID_HP_MP_NTF, ntf);
}

void GameServer::SendObjectVisibilityNtf(int64 userId,
                                         const std::vector<GamePacket::CharacterSpawnInfo>& characterSpawns,
                                         const std::vector<int64>& despawnIds)
{
    GamePacket::ObjectVisibilityNtf ntf;
    for (const auto& spawn : characterSpawns)
    {
        *ntf.add_character_spawns() = spawn;
    }
    for (int64 id : despawnIds)
    {
        ntf.add_despawn_ids(id);
    }

    sendPacketToUser(userId, Common::GAME_PACKET_ID_OBJECT_VISIBILITY_NTF, ntf);

    LOG_WRITE(LogLevel::Info, std::format("GameServer: ObjectVisibilityNtf sent. userId={} characterSpawns={} despawns={}",
        userId, characterSpawns.size(), despawnIds.size()));
}

void GameServer::SendMoveNtf(int64 userId, int64 objectId,
                             float posX, float posY, float posZ, float yaw,
                             float destX, float destY, float destZ, bool isMoving)
{
    GamePacket::MoveNtf ntf;
    ntf.set_object_id(objectId);
    ntf.set_pos_x(posX);
    ntf.set_pos_y(posY);
    ntf.set_pos_z(posZ);
    ntf.set_yaw(yaw);
    ntf.set_dest_x(destX);
    ntf.set_dest_y(destY);
    ntf.set_dest_z(destZ);
    ntf.set_is_moving(isMoving);

    sendPacketToUser(userId, Common::GAME_PACKET_ID_MOVE_NTF, ntf);
}

void GameServer::SendMovePosCorrectNtf(int64 userId, float posX, float posY, float posZ, float yaw)
{
    GamePacket::MovePosCorrectNtf ntf;
    ntf.set_pos_x(posX);
    ntf.set_pos_y(posY);
    ntf.set_pos_z(posZ);
    ntf.set_yaw(yaw);

    sendPacketToUser(userId, Common::GAME_PACKET_ID_MOVE_POS_CORRECT_NTF, ntf);

    LOG_WRITE(LogLevel::Info, std::format("GameServer: MovePosCorrectNtf sent. userId={} pos=({},{},{}) yaw={}",
        userId, posX, posY, posZ, yaw));
}

// 클라이언트 캐릭터 선택 요청 처리 → 코루틴
// 1) DB에서 (user_id, character_id) 로 캐릭터 조회
// 2) 없는 캐릭터면 CharacterSelectRes(Fail) 전송 후 종료
// 3) owner_user_id 검증 (DB 쇄괴 방어)
// 4) User에 현재 캐릭터 설정
// 5) SystemStage에서 제거 (UserLeave push)
// 6) Town으로 입장 (UserEnter push) → Town이 StageEnterNtf 전송
db::DetachedCoTask GameServer::handleClientCharacterSelect(int64 userId, GamePacket::CharacterSelectReq req)
{
    const int64 characterId = req.character_id();
    LOG_WRITE(LogLevel::Info, std::format("GameServer: CharacterSelectReq received. userId={} characterId={}",
        userId, characterId));

    // ── 1) DB에서 해당 캐릭터 조회 ─────────────────────────────
    db::DBResult result = co_await m_dbQueue.ExecuteAsync(
        "SELECT data FROM Characters WHERE user_id = ? AND character_id = ?",
        { userId, characterId },
        GetCoroutineResumeExecutor()
    );

    if (!result.success)
    {
        LOG_WRITE(LogLevel::Error, std::format("GameServer: CharacterSelect DB select failed. userId={} err={}", userId, result.errorMsg));
        sendCharacterSelectRes(userId, EResultCode::Fail, "server error: db select", characterId, 0, 0.f, 0.f, 0.f, 0.f);
        co_return;
    }

    // ── 2) 없는 캐릭터 ─────────────────────────────────────────────
    if (result.IsEmpty())
    {
        LOG_WRITE(LogLevel::Warn, std::format("GameServer: CharacterSelect - character not found. userId={} characterId={}",
            userId, characterId));
        sendCharacterSelectRes(userId, EResultCode::Fail, "character not found", characterId, 0, 0.f, 0.f, 0.f, 0.f);
        co_return;
    }

    DataStructures::Character character;
    const std::string dataJson = result.GetString(0, "data");
    if (!packet::ProtoJsonSerializer::FromJson(dataJson, character))
    {
        LOG_WRITE(LogLevel::Error, std::format("GameServer: CharacterSelect - failed to parse character JSON. userId={} characterId={}",
            userId, characterId));
        sendCharacterSelectRes(userId, EResultCode::Fail, "server error: parse", characterId, 0, 0.f, 0.f, 0.f, 0.f);
        co_return;
    }

    // ── 3) owner_user_id 검증 (PK가 (user_id, character_id) 이므로 이론상 통과해야 함, 안전장치) ──────────────────
    if (character.owner_user_id() != userId)
    {
        LOG_WRITE(LogLevel::Error, std::format("GameServer: CharacterSelect - owner mismatch. userId={} characterOwner={} characterId={}",
            userId, character.owner_user_id(), characterId));
        sendCharacterSelectRes(userId, EResultCode::Fail, "not character owner", characterId, 0, 0.f, 0.f, 0.f, 0.f);
        co_return;
    }

    // ── 4) User에 현재 캐릭터 설정 ──────────────────────────────────────
    UserPtr spUser;
    if (!m_safeUsers.Find(userId, spUser) || !spUser)
    {
        LOG_WRITE(LogLevel::Warn, std::format("GameServer: CharacterSelect - user not found in global map. userId={}", userId));
        co_return;
    }

    // Character 객체 생성. 라이프타임은 Town이 최종소유 (StageMsg_UserEnter로 전달 후).
    // 여기서는 임시로 shared_ptr을 들고 Town으로 넘겨주면 Town이 m_objects에 등록.
    CharacterPtr spCharacter = std::make_shared<Character>(character);
    spCharacter->SetUser(spUser);   // Character -> User weak_ptr
    spUser->SetCurrentCharacter(spCharacter);   // User -> Character weak_ptr

    LOG_WRITE(LogLevel::Info, std::format("GameServer: character selected. userId={} characterId={} name='{}'",
        userId, character.character_id(), character.name()));

    // ── 5) SystemStage에서 제거 ──────────────────────────────────────────
    if (SystemStagePtr spSystemStage = m_stageManager.GetSystemStage())
    {
        spSystemStage->EnqueueMessage(StageMsg_UserLeave{userId});
    }

    // ── 6) 클라에게 CharacterSelectRes(성공) 송신 ─────────────────────────
    // Town 으로의 입장은 EnqueueMessage 라 비동기지만, 클라는 이 응답을 받자마자
    // Game 씬 전환을 시작하면 됨. StageEnterNtf 는 Town 의 OnUserEnter 에서 별도 송신.
    TownPtr spTown = m_stageManager.GetTown();
    if (!spTown)
    {
        LOG_WRITE(LogLevel::Error, std::format("GameServer: CharacterSelect - Town is null. userId={}", userId));
        sendCharacterSelectRes(userId, EResultCode::Fail, "server error: no town", characterId, 0, 0.f, 0.f, 0.f, 0.f);
        co_return;
    }

    sendCharacterSelectRes(userId, EResultCode::Success, "",
        character.character_id(), spTown->GetStageId(),
        character.pos_x(), character.pos_y(), character.pos_z(), character.yaw());

    // ── 7) Town으로 입장 ───────────────────────────────────────────────
    // Town이 OnUserEnter override로 StageEnterNtf를 전송한다.
    spTown->EnqueueMessage(StageMsg_UserEnter{spUser, spCharacter});
}

void GameServer::sendCharacterSelectRes(int64 userId, EResultCode resultCode, const std::string& errorMsg,
                                        int64 characterId, int64 stageId,
                                        float posX, float posY, float posZ, float yaw)
{
    GamePacket::CharacterSelectRes res;
    res.set_result_code(static_cast<int32>(resultCode));
    res.set_error_msg(errorMsg);
    res.set_character_id(characterId);
    res.set_stage_id(stageId);
    res.set_pos_x(posX);
    res.set_pos_y(posY);
    res.set_pos_z(posZ);
    res.set_yaw(yaw);
    sendPacketToUser(userId, Common::GAME_PACKET_ID_CHARACTER_SELECT_RES, res);

    LOG_WRITE(LogLevel::Info, std::format("GameServer: CharacterSelectRes sent. userId={} resultCode={} characterId={} stageId={}",
        userId, static_cast<int32>(resultCode), characterId, stageId));
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

    // 유저가 속한 Stage에만 퇴장 메시지 push.
    const int64 currentStageId = spUser->GetCurrentStageId();
    if (StagePtr spStage = m_stageManager.Find(currentStageId))
    {
        spStage->EnqueueMessage(StageMsg_UserLeave{userId});
    }
    else
    {
        LOG_WRITE(LogLevel::Warn, std::format("GameServer: user disconnect - unknown stageId. userId={} stageId={}",
            userId, currentStageId));
    }
}

// 게이트웨이서버로부터 HandshakeRes를 받음
void GameServer::handleGatewayHandshakeRes(const netlib::ISessionPtr& spSession, const ServerPacket::ServerHandshakeRes& msg)
{
    int32 gatewayId = msg.server_id();

    if (!msg.success())
    {
        LOG_WRITE(LogLevel::Error, std::format("gateway handshake failed. gatewayId={} error='{}'", gatewayId, msg.error_msg()));
        return;
    }

    InternalSessionMeta* pMeta = getInternalSessionMeta(spSession);
    pMeta->handshakeDone = true;

    LOG_WRITE(LogLevel::Info, std::format("gateway handshake complete. gatewayId={}, sessionId={}", gatewayId, spSession->GetId()));
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

    // 캐릭터 선택/생성 단계 패킷은 GameServer가 직접 처리 (DB 코루틴 필요).
    // 그외 게임 플레이 패킷은 User 패킷큐에 push → Stage가 처리.
    const uint16 packetType = spPacket->GetHeader()->type;
    switch (packetType)
    {
    case Common::GAME_PACKET_ID_CHARACTER_CREATE_REQ:
    {
        GamePacket::CharacterCreateReq req;
        if (!DeserializePacket(*spPacket, req))
        {
            LOG_WRITE(LogLevel::Warn, std::format("GameServer: failed to deserialize CharacterCreateReq. userId={}", userId));
            return;
        }
        handleClientCharacterCreate(userId, std::move(req));
        return;
    }
    case Common::GAME_PACKET_ID_CHARACTER_SELECT_REQ:
    {
        GamePacket::CharacterSelectReq req;
        if (!DeserializePacket(*spPacket, req))
        {
            LOG_WRITE(LogLevel::Warn, std::format("GameServer: failed to deserialize CharacterSelectReq. userId={}", userId));
            return;
        }
        handleClientCharacterSelect(userId, std::move(req));
        return;
    }
    default:
        break;
    }

    // 기본: User 패킷 큐에 push (Stage 스레드가 다음 tick에서 drain해서 처리)
    spUser->EnqueuePacket(spPacket);
}
