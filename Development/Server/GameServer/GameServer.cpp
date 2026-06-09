#include "pch.h"
#include "GameServer.h"
#include "Character.h"

namespace
{
    constexpr const char* k_gameDBPath = "GameDB.db";
}

bool GameServer::OnInitialize()
{
    // ── 내부서버용 패킷 디스패처 ────────────────────────────────
    m_internalPacketDispatcher.SetUnknownPacketHandler([](const netlib::ISessionPtr& spSession, const netlib::PacketPtr& spPacket)
    {
        LOG_WRITE(LogLevel::Warn, std::format("unknown internal packetId={} sessionId={}", spPacket->GetHeader()->type, spSession->GetId()));
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
        LOG_WRITE(LogLevel::Warn, std::format("unknown gateway packetId={} sessionId={}", spPacket->GetHeader()->type, spSession->GetId()));
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
        LOG_WRITE(LogLevel::Error, std::format("failed to initialize NavMeshManager. navMeshDir={}", navMeshDir.string()));
        return false;
    }

    // ── StageManager 초기화 + 고정 Stage 생성 ──────────────────
    // StageManager가 Stage 생성 + SetGameServer + AssignContents까지 처리.
    if (GetContentsThreadCount() <= 0)
    {
        LOG_WRITE(LogLevel::Error, "no contents threads available.");
        return false;
    }

    m_stageManager.Initialize(this, GetContentsThreadCount());

    const int64 systemStageId = GenerateObjectId();
    if (!m_stageManager.CreateSystemStage(systemStageId, k_systemStageDataKey))
    {
        LOG_WRITE(LogLevel::Error, std::format("failed to create SystemStage. stageId={}, stageKey={}", systemStageId, k_systemStageDataKey));
        return false;
    }

    const int64 townStageId = GenerateObjectId();
    if (!m_stageManager.CreateTown(townStageId, k_townStageDataKey))
    {
        LOG_WRITE(LogLevel::Error, std::format("failed to create Town. stageId={}, stageKey={}", townStageId, k_townStageDataKey));
        return false;
    }

    // Field 타입 Stage는 GameData_Stage 데이터 기준으로 전부 생성 (데이터 추가만으로 필드 확장).
    for (const auto& [stageDataKey, pStageData] : GameDataTable_Stage::GetDataMap())
    {
        if (pStageData->StageType != EStageType::Field)
            continue;

        const int64 fieldStageId = GenerateObjectId();
        if (!m_stageManager.CreateField(fieldStageId, stageDataKey))
        {
            LOG_WRITE(LogLevel::Error, std::format("failed to create Field. stageId={}, stageKey={}", fieldStageId, stageDataKey));
            return false;
        }
    }

    // ── GameDB 열기 ────────────────────────────────────────────
    if (!m_dbQueue.Open(k_gameDBPath, 1))
    {
        LOG_WRITE(LogLevel::Error, std::format("failed to open GameDB at {}", k_gameDBPath));
        return false;
    }

    LOG_WRITE(LogLevel::Info, std::format("complete. serverId={}", GetServerId()));
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

    LOG_WRITE(LogLevel::Info, std::format("internal server connected. sessionId={}", spSession->GetId()));
    return true;
}

// 내부 서버 연결 끊김
void GameServer::onInternalDisconnect(const netlib::ISessionPtr& spSession)
{
    LOG_WRITE(LogLevel::Info, std::format("internal server disconnected. sessionId={}", spSession->GetId()));
}

// ──────────────────────────────────────────────────────────────
// 게이트웨이서버 연결
// ──────────────────────────────────────────────────────────────

void GameServer::onGatewayConnect(const netlib::ISessionPtr& spSession)
{
    // 세션에 빈 메타 정보를 부착한다. gatewayServerId는 handshake 전송 시 채운다.
    spSession->SetUserData(std::make_shared<InternalSessionMeta>());

    LOG_WRITE(LogLevel::Info, std::format("gateway connected. sessionId={}", spSession->GetId()));

    // 핸드셰이크 전송
    sendGameServerHandshakeReq(spSession);
}

void GameServer::onGatewayDisconnect(const netlib::ISessionPtr& spSession)
{
    // 세션 메타정보 peer 서버 ID 조회
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

void GameServer::connectToGateway(int32 gatewayId, const std::string& ip, uint16 port)
{
    if (m_safeGatewayClients.Contains(gatewayId))
        return;

    netlib::NetClientPtr spClient = ConnectToServer(ip, port, m_gatewayEventHandler);
    if (spClient)
    {
        m_safeGatewayClients.Insert(gatewayId, spClient);
        LOG_WRITE(LogLevel::Info, std::format("connecting to gateway {} {}:{}", gatewayId, ip, port));
    }
    else
    {
        LOG_WRITE(LogLevel::Warn, std::format("failed to create NetClient to gateway {} {}:{}", gatewayId, ip, port));
    }
}

void GameServer::disconnectFromGateway(int32 gatewayId)
{
    netlib::NetClientPtr spClient;
    if (!m_safeGatewayClients.EraseAndGet(gatewayId, spClient))
        return;

    if (spClient)
        DisconnectToServer(spClient);

    LOG_WRITE(LogLevel::Info, std::format("disconnected from gateway {}", gatewayId));
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
        LOG_WRITE(LogLevel::Warn, std::format("cannot identify gatewayId for session. sessionId={}", spGatewaySession->GetId()));
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
        LOG_WRITE(LogLevel::Error, std::format("failed to serialize GameServerHandshakeNtf. gatewayId={}", gatewayId));
        return;
    }

    spGatewaySession->Send(spPacket);

    LOG_WRITE(LogLevel::Info, std::format("sent GameServerHandshakeNtf. myServerId={} gatewayId={}", GetServerId(), gatewayId));
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

    LOG_WRITE(LogLevel::Info, std::format("GatewayUserEnterNtf received. userId={} gatewayId={} clientIp={}", userId, gatewayId, clientIp));

    // 이미 입장한 유저인지 확인
    if (m_safeUsers.Contains(userId))
    {
        LOG_WRITE(LogLevel::Warn, std::format("user already exists. userId={}", userId));
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
        LOG_WRITE(LogLevel::Error, std::format("DB select failed. userId={} err={}", userId, result.errorMsg));
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
            LOG_WRITE(LogLevel::Error, std::format("failed to parse character JSON. userId={} row={}", userId, row));
            continue;
        }
        characters.push_back(std::move(character));
    }

    LOG_WRITE(LogLevel::Info, std::format("characters loaded from DB. userId={} count={}", userId, characters.size()));

    // ── 2) 유저 객체 생성 및 글로벌 맵 등록 ────────────────────
    UserPtr spUser = std::make_shared<User>(userId, gatewayId, clientIp);
    m_safeUsers.Insert(userId, spUser);

    // ── 3) SystemStage(캐릭터 선택창)에 입장 메시지 push ──────────
    // 캐릭터 선택이 끝나면 handleClientCharacterSelect에서 Town으로 이동시킨다.
    SystemStagePtr spSystemStage = m_stageManager.GetSystemStage();
    if (spSystemStage)
    {
        spSystemStage->EnqueueMessage(StageMsg_UserEnter{spUser});
    }
    else
    {
        LOG_WRITE(LogLevel::Error, std::format("system stage is null. userId={}", userId));
        co_return;
    }

    // ── 4) GameEnterNtf + CharacterListNtf 응답 (게이트웨이 통해 클라에게) ──
    sendGameEnterNtf(userId);
    sendCharacterListNtf(userId, characters);
}

void GameServer::sendGameEnterNtf(int64 userId)
{
    GamePacket::GameEnterNtf ntf;
    ntf.set_stage_key(k_systemStageDataKey);

    SystemStagePtr spSystemStage = m_stageManager.GetSystemStage();
    if (spSystemStage)
    {
        ntf.set_stage_id(spSystemStage->GetStageId());
    }
    else
    {
        ntf.set_stage_id(0);
    }

    m_packetSender.SendToUser(userId, Common::GAME_PACKET_ID_GAME_ENTER_NTF, ntf);
}

void GameServer::sendCharacterListNtf(int64 userId, const std::vector<DataStructures::Character>& characters)
{
    GamePacket::CharacterListNtf ntf;
    for (const auto& character : characters)
    {
        *ntf.add_characters() = character;
    }
    m_packetSender.SendToUser(userId, Common::GAME_PACKET_ID_CHARACTER_LIST_NTF, ntf);

    LOG_WRITE(LogLevel::Info, std::format("CharacterListNtf sent. userId={} count={}", userId, characters.size()));
}

// 클라이언트 캐릭터 생성 요청 처리 → 코루틴
// 1) 명칭 검증 (빈 문자열만 거부, 상세 검증은 향후 추가)
// 2) ObjectId 발급 + 기본값 캐릭터 구성
// 3) DB INSERT
// 4) CharacterCreateRes 전송 (성공/실패)
db::DetachedCoTask GameServer::handleClientCharacterCreate(int64 userId, GamePacket::CharacterCreateReq req)
{
    LOG_WRITE(LogLevel::Info, std::format("CharacterCreateReq received. userId={} name='{}' jobId={}", userId, req.name(), req.job_id()));

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
    character.set_last_stage_id(0); // TBD
    character.set_pos_x(0.0f);
    character.set_pos_y(0.0f);
    character.set_pos_z(0.0f);
    character.set_yaw(0.0f);

    // ── 3) DB INSERT ──────────────────────────────────────────
    std::string dataJson;
    if (!packet::ProtoJsonSerializer::ToJson(character, dataJson))
    {
        LOG_WRITE(LogLevel::Error, std::format("failed to serialize character to JSON. userId={}", userId));
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
        LOG_WRITE(LogLevel::Error, std::format("CharacterCreate DB insert failed. userId={} err={}", userId, insertResult.errorMsg));
        sendCharacterCreateRes(userId, EResultCode::Fail, "server error: db insert", nullptr);
        co_return;
    }

    LOG_WRITE(LogLevel::Info, std::format("character created. userId={} characterId={} name='{}'",
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
    m_packetSender.SendToUser(userId, Common::GAME_PACKET_ID_CHARACTER_CREATE_RES, res);
}

// 클라이언트 캐릭터 선택 요청 처리 → 코루틴
// 1) DB에서 (user_id, character_id) 로 캐릭터 조회
// 2) 없는 캐릭터면 CharacterSelectRes(Fail) 전송 후 종료
// 3) owner_user_id 검증 (DB 쇄괴 방어)
// 4) User 조회 + 상태 검증, Character 객체 생성
// 5) CharacterSelectRes(전체 데이터) 송신 → 클라가 데이터모델 보관 + 로딩 시작
// 6) 2단계 입장(Moving) 시작: SystemStage 제거 + Town 입장
//    → 클라 StageLoadCompleteReq → Town이 spawn + StageLoadCompleteRes 전송
db::DetachedCoTask GameServer::handleClientCharacterSelect(int64 userId, GamePacket::CharacterSelectReq req)
{
    const int64 characterId = req.character_id();
    LOG_WRITE(LogLevel::Info, std::format("CharacterSelectReq received. userId={} characterId={}", userId, characterId));

    // ── 1) DB에서 해당 캐릭터 조회 ─────────────────────────────
    db::DBResult result = co_await m_dbQueue.ExecuteAsync(
        "SELECT data FROM Characters WHERE user_id = ? AND character_id = ?",
        { userId, characterId },
        GetCoroutineResumeExecutor()
    );

    if (!result.success)
    {
        LOG_WRITE(LogLevel::Error, std::format("CharacterSelect DB select failed. userId={} err={}", userId, result.errorMsg));
        sendCharacterSelectRes(userId, EResultCode::Fail, "server error: db select", nullptr, 0);
        co_return;
    }

    // ── 2) 없는 캐릭터 ─────────────────────────────────────────────
    if (result.IsEmpty())
    {
        LOG_WRITE(LogLevel::Warn, std::format("CharacterSelect - character not found. userId={} characterId={}", userId, characterId));
        sendCharacterSelectRes(userId, EResultCode::Fail, "character not found", nullptr, 0);
        co_return;
    }

    DataStructures::Character character;
    const std::string dataJson = result.GetString(0, "data");
    if (!packet::ProtoJsonSerializer::FromJson(dataJson, character))
    {
        LOG_WRITE(LogLevel::Error, std::format("CharacterSelect - failed to parse character JSON. userId={} characterId={}",
            userId, characterId));
        sendCharacterSelectRes(userId, EResultCode::Fail, "server error: parse", nullptr, 0);
        co_return;
    }

    // ── 3) owner_user_id 검증 (PK가 (user_id, character_id) 이므로 이론상 통과해야 함, 안전장치) ──────────────────
    if (character.owner_user_id() != userId)
    {
        LOG_WRITE(LogLevel::Error, std::format("CharacterSelect - owner mismatch. userId={} characterOwner={} characterId={}",
            userId, character.owner_user_id(), characterId));
        sendCharacterSelectRes(userId, EResultCode::Fail, "not character owner", nullptr, 0);
        co_return;
    }

    // ── 4) User 조회 + 상태 검증 ─────────────────────────────────────────
    UserPtr spUser;
    if (!m_safeUsers.Find(userId, spUser) || !spUser)
    {
        LOG_WRITE(LogLevel::Warn, std::format("CharacterSelect - user not found in global map. userId={}", userId));
        co_return;
    }

    // 이미 입장 중(Moving)이거나 입장 완료(InStage)면 중복 선택 거부.
    if (spUser->GetStageState() != EUserStageState::None)
    {
        LOG_WRITE(LogLevel::Warn, std::format("CharacterSelect - invalid stage state. userId={} state={}",
            userId, static_cast<int32>(spUser->GetStageState())));
        sendCharacterSelectRes(userId, EResultCode::Fail, "already entering or in stage", nullptr, 0);
        co_return;
    }

    // Character 객체 생성. User가 강한 소유자가 된다 (선택~로그아웃까지 유지).
    // Stage는 스폰되어 있는 동안만 같은 캐릭터를 컨테이너에 함께 보관한다.
    CharacterPtr spCharacter = std::make_shared<Character>();
    if (!spCharacter->Initialize(character))
    {
        LOG_WRITE(LogLevel::Error, std::format("CharacterSelect - Character Initialize failed. userId={} characterId={}", userId, characterId));
        sendCharacterSelectRes(userId, EResultCode::Fail, "server error: character init", nullptr, 0);
        co_return;
    }
    spCharacter->SetUser(spUser);              // Character -> User weak_ptr
    spUser->SetCurrentCharacter(spCharacter);  // User -> Character shared_ptr (소유)

    LOG_WRITE(LogLevel::Info, std::format("character selected. userId={} characterId={} name='{}'",
        userId, character.character_id(), character.name()));

    // ── 5) Town 확인 ─────────────────────────────────────────────────────
    TownPtr spTown = m_stageManager.GetTown();
    if (!spTown)
    {
        LOG_WRITE(LogLevel::Error, std::format("CharacterSelect - Town is null. userId={}", userId));
        sendCharacterSelectRes(userId, EResultCode::Fail, "server error: no town", nullptr, 0);
        co_return;
    }

    // ── 6) 클라에게 CharacterSelectRes(성공) 송신 → 클라는 전체 데이터로 LocalPlayer 데이터모델을 만들고 로딩 시작 ──
    // 스폰을 시작하는 EnqueueMessage 보다 먼저 Res 를 보낸다 (클라가 데이터모델을 먼저 갖춘 뒤 로딩하도록).
    sendCharacterSelectRes(userId, EResultCode::Success, "", &character, spTown->GetStageDataKey());

    // ── 7) 2단계 입장 시작 (Stage 이동과 동일한 골격) ─────────────────────
    // 캐릭터는 이미 User가 소유 중. Town에는 유저만 입장시키고, 스폰은 LoadComplete 시.
    // 클라가 Game 씬 + 맵 로딩 후 StageLoadCompleteReq를 보내면 Town이 스폰하고 StageLoadCompleteRes를 보낸다.
    // positionType=None → 캐릭터의 DB 좌표 사용 (마지막 위치 복귀).
    spUser->SetPendingPositionType(EStagePositionType::None);
    spUser->SetStageState(EUserStageState::Moving);

    if (SystemStagePtr spSystemStage = m_stageManager.GetSystemStage())
    {
        spSystemStage->EnqueueMessage(StageMsg_UserLeave{userId});
    }
    spTown->EnqueueMessage(StageMsg_UserEnter{spUser});
}

void GameServer::sendCharacterSelectRes(int64 userId, EResultCode resultCode, const std::string& errorMsg,
                                        const DataStructures::Character* pCharacter, int32 stageDataKey)
{
    GamePacket::CharacterSelectRes res;
    res.set_result_code(static_cast<int32>(resultCode));
    res.set_error_msg(errorMsg);
    if (pCharacter)
        *res.mutable_character() = *pCharacter;
    res.set_stage_data_key(stageDataKey);
    m_packetSender.SendToUser(userId, Common::GAME_PACKET_ID_CHARACTER_SELECT_RES, res);

    LOG_WRITE(LogLevel::Info, std::format("CharacterSelectRes sent. userId={} resultCode={} characterId={} stageDataKey={}",
        userId, static_cast<int32>(resultCode), pCharacter ? pCharacter->character_id() : 0, stageDataKey));
}

// 게이트웨이로부터 GatewayUserDisconnectNtf 수신
// → 글로벌 맵에서 제거, 현재 Stage에 퇴장 메시지 push
void GameServer::handleGatewayUserDisconnect(const netlib::ISessionPtr& /*spSession*/, const ServerPacket::GatewayUserDisconnectNtf& msg)
{
    const int64 userId = msg.user_id();

    LOG_WRITE(LogLevel::Info, std::format("GatewayUserDisconnectNtf received. userId={}", userId));

    UserPtr spUser;
    if (!m_safeUsers.EraseAndGet(userId, spUser) || !spUser)
    {
        LOG_WRITE(LogLevel::Warn, std::format("user not found on disconnect. userId={}", userId));
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
        LOG_WRITE(LogLevel::Warn, std::format("user disconnect - unknown stageId. userId={} stageId={}", userId, currentStageId));
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
        LOG_WRITE(LogLevel::Warn, std::format("unexpected sidecar size. expected={} actual={} packetType={}",
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
        LOG_WRITE(LogLevel::Warn, std::format("relayed client packet for unknown user. userId={} packetType={}",
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
            LOG_WRITE(LogLevel::Warn, std::format("failed to deserialize CharacterCreateReq. userId={}", userId));
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
            LOG_WRITE(LogLevel::Warn, std::format("failed to deserialize CharacterSelectReq. userId={}", userId));
            return;
        }
        handleClientCharacterSelect(userId, std::move(req));
        return;
    }
    default:
        break;
    }

    // 게임 플레이 패킷: User 패킷 큐에 push. 유저가 속한 Stage가 다음 tick에 drain하여 처리.
    spUser->EnqueuePacket(spPacket);
}
