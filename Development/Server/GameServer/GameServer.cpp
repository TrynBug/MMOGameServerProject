#include "pch.h"
#include "GameServer.h"
#include "StageObjects/Character.h"
#include "DbSaveExecutor.h"

// ── [치트] 패킷 로깅 헬퍼 정의 (선언은 GameServerDefine.h) ──────────────
namespace packetlog
{
    EPacketLogMode EffectiveMode(const User& user)
    {
        const EPacketLogMode userMode   = user.GetCheatPacketLogMode();
        const EPacketLogMode globalMode = GameServer::Instance().GetCheatManager().GetGlobalPacketLogMode();
        return (userMode >= globalMode) ? userMode : globalMode;   // None < Name < Detail
    }

    void LogPacket(const char* dir, int64 accountId, uint16 packetType, const google::protobuf::Message* pMsg)
    {
        const std::string& name = Common::GamePacketId_Name(static_cast<Common::GamePacketId>(packetType));
        const char* pName = name.empty() ? "UNKNOWN" : name.c_str();

        if (pMsg != nullptr)
        {
            std::string json;
            packet::ProtoJsonSerializer::ToJson(*pMsg, json);
            LOG_WRITE(LogLevel::Info, std::format("[PKT][{}] uid={} {}({}) {}", dir, accountId, pName, packetType, json));
        }
        else
        {
            LOG_WRITE(LogLevel::Info, std::format("[PKT][{}] uid={} {}({})", dir, accountId, pName, packetType));
        }
    }
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

    m_gatewayDispatcher.Register<ServerPacket::GatewayUserRerouteNtf>(Common::SERVER_PACKET_ID_USER_REROUTE_NTF,
        [this](auto& spSession, auto& msg) { handleGatewayUserReroute(spSession, msg); });

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
        // (게이트웨이가 원본 클라 패킷에 accountId 사이드카를 더해서 전송함)
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

    // StageAssetManager 초기화 (모든 Stage 의 레이아웃 + 스크립트 바이트코드 선로드/검증).
    // 명시된 스크립트 파일 누락/컴파일 실패 시 fail-fast.
    if (!m_stageAssetManager.LoadAll())
    {
        LOG_WRITE(LogLevel::Error, "failed to initialize StageAssetManager (missing/invalid stage script or layout).");
        return false;
    }

    // ── StageManager 초기화 + 고정 Stage 생성 ──────────────────
    // StageManager가 Stage 생성 + SetGameServer + AssignContents까지 처리.
    if (GetContentsThreadCount() <= 0)
    {
        LOG_WRITE(LogLevel::Error, "no contents threads available.");
        return false;
    }

    m_stageManager.Initialize(GetContentsThreadCount());

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
}

void GameServer::OnShutdown()
{
    LOG_WRITE(LogLevel::Info, "GameServer::OnShutdown");
}

// AccountDB에서 계정(DataStructures::Account)을 읽는다. 실패/미발견 시 nullopt.
db::AwaitableCoTask<std::optional<DataStructures::Account>> GameServer::loadAccount(int64 accountId)
{
    db::DBResult result = co_await GetDB().ExecuteAsync(
        db::EDBType::Account, 0,
        "SELECT data FROM Accounts WHERE account_id = ?",
        { accountId },
        GetCoroutineResumeExecutor()
    );

    if (!result.success || result.IsEmpty())
    {
        LOG_WRITE(LogLevel::Error, std::format("loadAccount - account not found. accountId={} success={}", accountId, result.success));
        co_return std::nullopt;
    }

    DataStructures::Account account;
    if (!packet::ProtoJsonSerializer::FromJson(result.GetString(0, "data"), account))
    {
        LOG_WRITE(LogLevel::Error, std::format("loadAccount - parse failed. accountId={}", accountId));
        co_return std::nullopt;
    }

    co_return account;
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

// 특정 게이트웨이로 서버패킷 전송. 세션 없으면 false.
bool GameServer::SendToGateway(int32 gatewayId, const netlib::PacketPtr& spPacket)
{
    netlib::ISessionPtr spSession;
    if (!m_safeGatewaySessions.Find(gatewayId, spSession) || !spSession)
        return false;

    spSession->Send(spPacket);
    return true;
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
db::DetachedCoTask GameServer::handleGatewayUserEnter(netlib::ISessionPtr /*spSession*/, ServerPacket::GatewayUserEnterNtf msg)
{
    const int64 accountId    = msg.account_id();
    const int32 gatewayId = msg.gateway_id();
    const std::string clientIp = msg.client_ip();

    LOG_WRITE(LogLevel::Info, std::format("GatewayUserEnterNtf received. accountId={} gatewayId={} clientIp={}", accountId, gatewayId, clientIp));

    // 이미 입장한 유저인지 확인
    if (m_safeUsers.Contains(accountId))
    {
        LOG_WRITE(LogLevel::Warn, std::format("user already exists. accountId={}", accountId));
        co_return;
    }

    // AccountDB에서 계정 로드, game_db_index로 샤드 번호 얻음
    auto accountOpt = co_await loadAccount(accountId);
    if (!accountOpt)
    {
        LOG_WRITE(LogLevel::Error, std::format("enter - account load failed. accountId={}", accountId));
        co_return;
    }
    const int32 gameDbIndex = accountOpt->game_db_index();
    if (!GetDB().HasDatabase(db::EDBType::Game, gameDbIndex))
    {
        LOG_WRITE(LogLevel::Error, std::format("enter - invalid GameDB shard. accountId={} gameDbIndex={}", accountId, gameDbIndex));
        co_return;
    }

    // 해당 샤드에서 캐릭터 목록 조회
    db::DBResult result = co_await GetDB().ExecuteAsync(
        db::EDBType::Game, gameDbIndex,
        "SELECT data FROM Characters WHERE account_id = ?",
        { accountId },
        GetCoroutineResumeExecutor()
    );

    if (!result.success)
    {
        LOG_WRITE(LogLevel::Error, std::format("DB select failed. accountId={} err={}", accountId, result.errorMsg));
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
            LOG_WRITE(LogLevel::Error, std::format("failed to parse character JSON. accountId={} row={}", accountId, row));
            continue;
        }
        characters.push_back(std::move(character));
    }

    LOG_WRITE(LogLevel::Info, std::format("characters loaded from DB. accountId={} count={}", accountId, characters.size()));

    // 유저 객체 생성 및 글로벌 맵 등록
    UserPtr spUser = std::make_shared<User>(accountId, gatewayId, clientIp);
    spUser->SetAccount(*accountOpt);
    m_safeUsers.Insert(accountId, spUser);

    // SystemStage(캐릭터 선택창)에 입장 메시지 push
    // 캐릭터 선택이 끝나면 handleClientCharacterSelect에서 Town으로 이동시킨다.
    SystemStagePtr spSystemStage = m_stageManager.GetSystemStage();
    if (spSystemStage)
    {
        spSystemStage->EnqueueMessage(StageMsg_UserEnter{spUser});
    }
    else
    {
        LOG_WRITE(LogLevel::Error, std::format("system stage is null. accountId={}", accountId));
        co_return;
    }

    // GameEnterNtf + CharacterListNtf 응답 (게이트웨이 통해 클라에게)
    sendGameEnterNtf(accountId);
    sendCharacterListNtf(accountId, characters);
}

void GameServer::sendGameEnterNtf(int64 accountId)
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

    m_packetSender.SendToUser(accountId, Common::GAME_PACKET_ID_GAME_ENTER_NTF, ntf);
}

void GameServer::sendCharacterListNtf(int64 accountId, const std::vector<DataStructures::Character>& characters)
{
    GamePacket::CharacterListNtf ntf;
    for (const auto& character : characters)
    {
        *ntf.add_characters() = character;
    }
    m_packetSender.SendToUser(accountId, Common::GAME_PACKET_ID_CHARACTER_LIST_NTF, ntf);

    LOG_WRITE(LogLevel::Info, std::format("CharacterListNtf sent. accountId={} count={}", accountId, characters.size()));
}

// 클라이언트 캐릭터 생성 요청 처리 → 코루틴
db::DetachedCoTask GameServer::handleClientCharacterCreate(int64 accountId, GamePacket::CharacterCreateReq req)
{
    LOG_WRITE(LogLevel::Info, std::format("CharacterCreateReq received. accountId={} name='{}' jobId={}", accountId, req.name(), req.job_id()));

    // 명칭 검증
    if (req.name().empty())
    {
        sendCharacterCreateRes(accountId, EResultCode::Fail, "name is empty", nullptr);
        co_return;
    }

    // 유저
    UserPtr spUser;
    if (!m_safeUsers.Find(accountId, spUser) || !spUser)
    {
        LOG_WRITE(LogLevel::Warn, std::format("CharacterCreate - user not found. accountId={}", accountId));
        sendCharacterCreateRes(accountId, EResultCode::Fail, "user not found", nullptr);
        co_return;
    }
    const int32 gameDbIndex = spUser->GetGameDbIndex();
    if (!GetDB().HasDatabase(db::EDBType::Game, gameDbIndex))
    {
        LOG_WRITE(LogLevel::Error, std::format("CharacterCreate - invalid shard. accountId={} idx={}", accountId, gameDbIndex));
        sendCharacterCreateRes(accountId, EResultCode::Fail, "server error: shard", nullptr);
        co_return;
    }

    // ── 캐릭터 구성 ──────────────────────────────────────
    DataStructures::Character character;
    character.set_character_id(GenerateObjectId());
    character.set_owner_account_id(accountId);
    character.set_name(req.name());
    character.set_job_id(req.job_id());
    character.set_level(1);
    character.set_exp(0);
    character.set_last_stage_id(0); // TBD
    character.set_pos_x(0.0f);
    character.set_pos_y(0.0f);
    character.set_pos_z(0.0f);
    character.set_yaw(0.0f);

    // ── DB INSERT ──────────────────────────────────────────
    std::string dataJson;
    if (!packet::ProtoJsonSerializer::ToJson(character, dataJson))
    {
        LOG_WRITE(LogLevel::Error, std::format("failed to serialize character to JSON. accountId={}", accountId));
        sendCharacterCreateRes(accountId, EResultCode::Fail, "server error: serialize", nullptr);
        co_return;
    }

    db::DBResult insertResult = co_await GetDB().ExecuteAsync(
        db::EDBType::Game, gameDbIndex,
        "INSERT INTO Characters (account_id, character_id, data) VALUES (?, ?, ?)",
        { accountId, character.character_id(), dataJson },
        GetCoroutineResumeExecutor()
    );

    if (!insertResult.success)
    {
        LOG_WRITE(LogLevel::Error, std::format("CharacterCreate DB insert failed. accountId={} err={}", accountId, insertResult.errorMsg));
        sendCharacterCreateRes(accountId, EResultCode::Fail, "server error: db insert", nullptr);
        co_return;
    }

    LOG_WRITE(LogLevel::Info, std::format("character created. accountId={} characterId={} name='{}'",
        accountId, character.character_id(), character.name()));

    // ── 성공 응답 ────────────────────────────────────────────
    sendCharacterCreateRes(accountId, EResultCode::Success, "", &character);
}

void GameServer::sendCharacterCreateRes(int64 accountId, EResultCode resultCode, const std::string& errorMsg, const DataStructures::Character* pNewCharacter)
{
    GamePacket::CharacterCreateRes res;
    res.set_result_code(static_cast<int32>(resultCode));
    res.set_error_msg(errorMsg);
    if (pNewCharacter)
    {
        *res.mutable_new_character() = *pNewCharacter;
    }
    m_packetSender.SendToUser(accountId, Common::GAME_PACKET_ID_CHARACTER_CREATE_RES, res);
}

// DB에서 캐릭터 row를 읽어 JSON 파싱 후 Character 객체를 생성하여 User에 소유 연결한다.
// User가 강한 소유자(shared_ptr), Character→User는 약참조(weak_ptr).
// 조회 실패/없음/파싱 실패/소유자 불일치/Initialize 실패 시 nullptr(사유는 내부 로그).
// 호출자(캐릭터 선택 / 크로스서버 이동 입장)는 각자 프로토콜에 맞는 실패 응답을 처리한다.
db::AwaitableCoTask<CharacterPtr> GameServer::loadCharacterForUser(int64 accountId, int64 characterId, UserPtr spUser)
{
    // ── 유저의 GameDB 샤드 해석 (호출 전에 spUser->GetGameDbIndex가 세팅돼 있어야 함) ──
    const int32 gameDbIndex = spUser->GetGameDbIndex();
    if (!GetDB().HasDatabase(db::EDBType::Game, gameDbIndex))
    {
        LOG_WRITE(LogLevel::Error, std::format("loadCharacter - invalid shard. accountId={} idx={}", accountId, gameDbIndex));
        co_return nullptr;
    }

    // ── DB에서 캐릭터 조회 ──
    db::DBResult result = co_await GetDB().ExecuteAsync(
        db::EDBType::Game, gameDbIndex,
        "SELECT data FROM Characters WHERE account_id = ? AND character_id = ?",
        { accountId, characterId },
        GetCoroutineResumeExecutor()
    );

    if (!result.success || result.IsEmpty())
    {
        LOG_WRITE(LogLevel::Warn, std::format("loadCharacter - select failed/empty. accountId={} characterId={} success={}",
            accountId, characterId, result.success));
        co_return nullptr;
    }

    // ── JSON 파싱 ──
    DataStructures::Character protoData;
    if (!packet::ProtoJsonSerializer::FromJson(result.GetString(0, "data"), protoData))
    {
        LOG_WRITE(LogLevel::Error, std::format("loadCharacter - parse failed. accountId={} characterId={}", accountId, characterId));
        co_return nullptr;
    }

    // owner_account_id 검증 (PK가 (account_id, character_id) 이므로 통상 통과. DB 손상 방어).
    if (protoData.owner_account_id() != accountId)
    {
        LOG_WRITE(LogLevel::Error, std::format("loadCharacter - owner mismatch. accountId={} owner={} characterId={}",
            accountId, protoData.owner_account_id(), characterId));
        co_return nullptr;
    }

    // ── Character 객체 생성 + User 소유 연결 ──
    CharacterPtr spCharacter = std::make_shared<Character>();
    if (!spCharacter->Initialize(protoData))
    {
        LOG_WRITE(LogLevel::Error, std::format("loadCharacter - Initialize failed. accountId={} characterId={}", accountId, characterId));
        co_return nullptr;
    }
    spCharacter->SetUser(spUser);              // Character -> User weak_ptr
    spUser->SetCurrentCharacter(spCharacter);  // User -> Character shared_ptr (소유)

    co_return spCharacter;
}

// 클라이언트 캐릭터 선택 요청 처리 → 코루틴
// 1) User 조회 + 상태 검증 (None 상태만 허용, DB 조회 전에 값싼 검증 먼저)
// 2) DB에서 캐릭터 로드 + Character 객체 생성 (loadCharacterForUser 공통 사용)
// 3) CharacterSelectRes(전체 데이터) 송신 → 클라가 데이터모델 보관 + 로딩 시작
// 4) 2단계 입장(Moving) 시작: SystemStage 제거 + Town 입장
//    → 클라 StageLoadCompleteReq → Town이 spawn + StageLoadCompleteRes 전송
db::DetachedCoTask GameServer::handleClientCharacterSelect(int64 accountId, GamePacket::CharacterSelectReq req)
{
    const int64 characterId = req.character_id();
    LOG_WRITE(LogLevel::Info, std::format("CharacterSelectReq received. accountId={} characterId={}", accountId, characterId));

    // ── 1) User 조회 + 상태 검증 (DB 조회 전에 값싼 검증 먼저) ──
    UserPtr spUser;
    if (!m_safeUsers.Find(accountId, spUser) || !spUser)
    {
        LOG_WRITE(LogLevel::Warn, std::format("CharacterSelect - user not found in global map. accountId={}", accountId));
        co_return;
    }

    // 이미 입장 중(Moving)이거나 입장 완료(InStage)면 중복 선택 거부.
    if (spUser->GetStageState() != EUserStageState::None)
    {
        LOG_WRITE(LogLevel::Warn, std::format("CharacterSelect - invalid stage state. accountId={} state={}",
            accountId, static_cast<int32>(spUser->GetStageState())));
        sendCharacterSelectRes(accountId, EResultCode::Fail, "already entering or in stage", nullptr, 0);
        co_return;
    }

    // ── 2) DB에서 캐릭터 로드 + Character 객체 생성 (loadCharacterForUser 공통 사용) ──
    // User가 강한 소유자가 된다 (선택~로그아웃까지 유지). Stage는 스폰되어 있는 동안만 함께 보관.
    CharacterPtr spCharacter = co_await loadCharacterForUser(accountId, characterId, spUser);
    if (!spCharacter)
    {
        sendCharacterSelectRes(accountId, EResultCode::Fail, "character load failed", nullptr, 0);
        co_return;
    }

    // 클라 응답(CharacterSelectRes)/로깅에 실을 캐릭터 데이터는 생성된 객체에서 되찾는다.
    const DataStructures::Character& character = spCharacter->GetProto();
    LOG_WRITE(LogLevel::Info, std::format("character selected. accountId={} characterId={} name='{}'",
        accountId, character.character_id(), character.name()));

    // ── 5) Town 확인 ─────────────────────────────────────────────────────
    TownPtr spTown = m_stageManager.GetTown();
    if (!spTown)
    {
        LOG_WRITE(LogLevel::Error, std::format("CharacterSelect - Town is null. accountId={}", accountId));
        sendCharacterSelectRes(accountId, EResultCode::Fail, "server error: no town", nullptr, 0);
        co_return;
    }

    // ── 6) 클라에게 CharacterSelectRes(성공) 송신 → 클라는 전체 데이터로 LocalPlayer 데이터모델을 만들고 로딩 시작 ──
    // 스폰을 시작하는 EnqueueMessage 보다 먼저 Res 를 보낸다 (클라가 데이터모델을 먼저 갖춘 뒤 로딩하도록).
    sendCharacterSelectRes(accountId, EResultCode::Success, "", &character, spTown->GetStageDataKey());

    // ── 7) 2단계 입장 시작 (Stage 이동과 동일한 골격) ─────────────────────
    // 캐릭터는 이미 User가 소유 중. Town에는 유저만 입장시키고, 스폰은 LoadComplete 시.
    // 클라가 Game 씬 + 맵 로딩 후 StageLoadCompleteReq를 보내면 Town이 스폰하고 StageLoadCompleteRes를 보낸다.
    // positionType=None → 캐릭터의 DB 좌표 사용 (마지막 위치 복귀).
    spUser->SetPendingPositionType(EStagePositionType::None);
    spUser->SetStageState(EUserStageState::Moving);

    if (SystemStagePtr spSystemStage = m_stageManager.GetSystemStage())
    {
        spSystemStage->EnqueueMessage(StageMsg_UserLeave{accountId});
    }
    spTown->EnqueueMessage(StageMsg_UserEnter{spUser});
}

void GameServer::sendCharacterSelectRes(int64 accountId, EResultCode resultCode, const std::string& errorMsg,
                                        const DataStructures::Character* pCharacter, int32 stageDataKey)
{
    GamePacket::CharacterSelectRes res;
    res.set_result_code(static_cast<int32>(resultCode));
    res.set_error_msg(errorMsg);
    if (pCharacter)
        *res.mutable_character() = *pCharacter;
    res.set_stage_data_key(stageDataKey);
    m_packetSender.SendToUser(accountId, Common::GAME_PACKET_ID_CHARACTER_SELECT_RES, res);

    LOG_WRITE(LogLevel::Info, std::format("CharacterSelectRes sent. accountId={} resultCode={} characterId={} stageDataKey={}",
        accountId, static_cast<int32>(resultCode), pCharacter ? pCharacter->character_id() : 0, stageDataKey));
}

// 게이트웨이로부터 GatewayUserDisconnectNtf 수신
// → 글로벌 맵에서 제거, 현재 Stage에 퇴장 메시지 push
void GameServer::handleGatewayUserDisconnect(const netlib::ISessionPtr& /*spSession*/, const ServerPacket::GatewayUserDisconnectNtf& msg)
{
    const int64 accountId = msg.account_id();

    LOG_WRITE(LogLevel::Info, std::format("GatewayUserDisconnectNtf received. accountId={}", accountId));

    UserPtr spUser;
    if (!m_safeUsers.EraseAndGet(accountId, spUser) || !spUser)
    {
        LOG_WRITE(LogLevel::Warn, std::format("user not found on disconnect. accountId={}", accountId));
        return;
    }

    // 유저가 속한 Stage에만 퇴장 메시지 push.
    const int64 currentStageId = spUser->GetCurrentStageId();
    if (StagePtr spStage = m_stageManager.Find(currentStageId))
    {
        spStage->EnqueueMessage(StageMsg_UserLeave{accountId});
    }
    else
    {
        LOG_WRITE(LogLevel::Warn, std::format("user disconnect - unknown stageId. accountId={} stageId={}", accountId, currentStageId));
    }
}

// ──────────────────────────────────────────────────────────────
// 크로스서버 이동 (DB 경유)
// ──────────────────────────────────────────────────────────────
//
// 흐름:
//   출발 서버 A: Stage::handleStageMoveReq(크로스서버 분기, 유저는 Stage에 남겨둠) → BeginCrossServerMove
//                → 캐릭터 DB 저장 → (성공) 게이트웨이에 UserMoveToGameServerReq → A의 Stage에서 퇴장 + A에서 유저 제거
//                → (실패) InStage 복귀 + StageMoveRes(실패). 유저가 Stage를 떠난 적 없어 무소속 유저가 안 생긴다.
//   게이트웨이:  routedGameServerId = B 로 변경 → B에 GatewayUserRerouteNtf
//   목적지 서버 B: handleGatewayUserReroute → DB 로드 → User/Character 생성 → 대상 Stage 입장(Moving)
//                → 클라에 StageMoveRes(성공)
//   클라:        StageMoveRes(성공) → 맵 로딩 → StageLoadCompleteReq(게이트웨이가 B로 라우팅)
//                → B의 Stage가 스폰 + StageLoadCompleteRes (로컬 이동과 동일)
//
// HP/MP/버프/쿨다운 등 휘발성 상태는 현재 Character proto에 저장되지 않으므로 이동 시 직업기본 최대치로
// 리셋된다(캐릭터 재선택과 동일). v1 한정. 보존하려면 proto에 필드를 추가해야 한다.

// 캐릭터의 런타임 상태를 proto에 동기화한 뒤 퍼시스턴스 배치로 통째 upsert 한다([B] 주기/강제 저장 경로).
// account_id/character_id 는 캐릭터 proto에서 얻는다. 직렬화 실패/DB 실패 시 false(사유는 내부 로그).
db::AwaitableCoTask<bool> GameServer::saveCharacterToDB(CharacterPtr spCharacter, db::IResumeExecutor* pResumeExecutor)
{
    // 런타임 좌표/yaw 등을 proto에 반영한 뒤 직렬화.
    spCharacter->SyncRuntimeToProto();
    const DataStructures::Character& proto = spCharacter->GetProto();
    const int64 accountId      = proto.owner_account_id();
    const int64 characterId = proto.character_id();

    // 샤드는 캐릭터의 소유 유저(Character→User)의 game_db_index로 선택한다.
    UserPtr spUser = spCharacter->GetUserWeak().lock();
    if (!spUser)
    {
        LOG_WRITE(LogLevel::Error, std::format("saveCharacter - owner user gone. accountId={} characterId={}", accountId, characterId));
        co_return false;
    }
    const int32 gameDbIndex = spUser->GetGameDbIndex();

    // Character(상주 상태)를 배치로 통째 upsert. 호출부는 (proto, accountId, characterId)만 넘기면 된다.
    // live proto의 private copy를 shared_ptr로 담는다(직렬화는 실행기가 write 직전에 — 불변식: in-flight 중 수정 금지).
    auto spBatch = std::make_shared<db::DbSaveBatch>();
    spBatch->Upsert(std::make_shared<DataStructures::Character>(proto), accountId, characterId);

    db::DBResult result = co_await db::DbSaveExecutor::Save(GetDB(), spBatch, gameDbIndex, pResumeExecutor);
    if (!result.success)
    {
        LOG_WRITE(LogLevel::Error, std::format("saveCharacter - DB save failed. accountId={} characterId={} err={}",
            accountId, characterId, result.errorMsg));
        co_return false;
    }

    co_return true;
}

// 출발 서버: 크로스서버 이동 개시. 호출 시점에 유저는 아직 출발 Stage에 남아있다.
// pSourceStage: 출발 Stage. DB await 후속작업이 이 Stage의 컨텐츠 스레드에서 재개되고, 성공 시 퇴장도 이 Stage로 enqueue된다.
db::DetachedCoTask GameServer::BeginCrossServerMove(int64 accountId, int32 targetGameServerId, int32 targetStageDataKey, int32 positionType, Stage* pSourceStage)
{
    UserPtr spUser;
    if (!m_safeUsers.Find(accountId, spUser) || !spUser)
    {
        LOG_WRITE(LogLevel::Warn, std::format("CrossServerMove - user not found. accountId={}", accountId));
        co_return;
    }

    CharacterPtr spCharacter = spUser->GetCurrentCharacter();
    if (!spCharacter)
    {
        LOG_WRITE(LogLevel::Error, std::format("CrossServerMove - no character. accountId={}", accountId));
        co_return;
    }

    const int64 characterId = spCharacter->GetProto().character_id();

    // ── 1) 캐릭터를 DB에 저장 (saveCharacterToDB 공통 사용) ──
    // 유저는 아직 출발 Stage에 남아있다. 후속작업이 출발 Stage의 컨텐츠 스레드에서 재개되도록 그 executor를 넘긴다.
    // 실패하면 유저가 Stage를 떠난 적이 없으므로 InStage 복귀로 롤백한다(무소속 유저 없음).
    if (!co_await saveCharacterToDB(spCharacter, pSourceStage->GetResumeExecutor()))
    {
        spUser->SetStageState(EUserStageState::InStage);
        m_packetSender.SendStageMoveRes(accountId, EResultCode::Fail, "server error: db save", targetStageDataKey);
        co_return;
    }

    // ── 2) 게이트웨이에 이동 요청 (게이트웨이가 목적지 게임서버로 재라우팅) ──
    ServerPacket::UserMoveToGameServerReq req;
    req.set_account_id(accountId);
    req.set_target_game_server_id(targetGameServerId);
    req.set_character_id(characterId);
    req.set_target_stage_data_key(targetStageDataKey);
    req.set_position_type(positionType);

    netlib::PacketPtr spPacket = SerializePacket(Common::SERVER_PACKET_ID_USER_MOVE_TO_GAME_SERVER_REQ, req);
    if (!spPacket || !SendToGateway(spUser->GetGatewayId(), spPacket))
    {
        LOG_WRITE(LogLevel::Error, std::format("CrossServerMove - send to gateway failed. accountId={} gatewayId={}", accountId, spUser->GetGatewayId()));
        spUser->SetStageState(EUserStageState::InStage);   // 아직 출발 Stage에 그대로 → 롤백
        m_packetSender.SendStageMoveRes(accountId, EResultCode::Fail, "server error: gateway route", targetStageDataKey);
        co_return;
    }

    // ── 3) 확정(point of no return). 이제 출발 Stage에서 퇴장 + 글로벌맵 제거. ──
    // 퇴장은 출발 Stage 스레드에서 처리되도록 메시지로 큐잉(다음 tick에 OnUserLeave → AOI despawn).
    // 글로벌맵에서 빼도 출발 Stage의 m_users가 다음 tick까지 캐릭터를 살려둔다.
    pSourceStage->EnqueueMessage(StageMsg_UserLeave{ accountId });
    UserPtr removed;
    m_safeUsers.EraseAndGet(accountId, removed);   // 글로벌 맵에서 제거

    LOG_WRITE(LogLevel::Info, std::format("CrossServerMove - handed off. accountId={} characterId={} -> gameServerId={} stageKey={}",
        accountId, characterId, targetGameServerId, targetStageDataKey));
}

// 목적지 서버: 게이트웨이가 재라우팅한 유저를 받아 DB에서 캐릭터를 로드하고 대상 Stage에 입장시킨다.
db::DetachedCoTask GameServer::handleGatewayUserReroute(netlib::ISessionPtr /*spSession*/, ServerPacket::GatewayUserRerouteNtf msg)
{
    const int64       accountId             = msg.account_id();
    const int32       gatewayId          = msg.gateway_id();
    const std::string clientIp           = msg.client_ip();
    const int64       characterId        = msg.character_id();
    const int32       targetStageDataKey = msg.target_stage_data_key();
    const auto        positionType       = static_cast<EStagePositionType>(msg.position_type());

    LOG_WRITE(LogLevel::Info, std::format("GatewayUserRerouteNtf received. accountId={} characterId={} stageKey={} gatewayId={}",
        accountId, characterId, targetStageDataKey, gatewayId));

    if (m_safeUsers.Contains(accountId))
    {
        LOG_WRITE(LogLevel::Warn, std::format("reroute - user already exists. accountId={}", accountId));
        co_return;
    }

    // ── User 생성 + DB에서 캐릭터 로드 + Character 객체 생성 (loadCharacterForUser 공통 사용) ──
    // StageMoveRes 송신은 유저가 글로벌맵(m_safeUsers)에 있어야 게이트웨이를 찾으므로,
    // 실패 시에도 응답을 먼저 보낸 다음 유저를 제거한다.
    UserPtr spUser = std::make_shared<User>(accountId, gatewayId, clientIp);
    m_safeUsers.Insert(accountId, spUser);

    // 이 계정의 Account 로드 + 보관
    auto accountOpt = co_await loadAccount(accountId);
    if (!accountOpt)
    {
        LOG_WRITE(LogLevel::Error, std::format("reroute - account load failed. accountId={}", accountId));
        m_packetSender.SendStageMoveRes(accountId, EResultCode::Fail, "server error: account", targetStageDataKey);
        UserPtr removed; m_safeUsers.EraseAndGet(accountId, removed);
        co_return;
    }
    const int32 gameDbIndex = accountOpt->game_db_index();
    if (!GetDB().HasDatabase(db::EDBType::Game, gameDbIndex))
    {
        LOG_WRITE(LogLevel::Error, std::format("reroute - invalid GameDB shard. accountId={} idx={}", accountId, gameDbIndex));
        m_packetSender.SendStageMoveRes(accountId, EResultCode::Fail, "server error: shard", targetStageDataKey);
        UserPtr removed; m_safeUsers.EraseAndGet(accountId, removed);
        co_return;
    }
    spUser->SetAccount(*accountOpt);

    CharacterPtr spCharacter = co_await loadCharacterForUser(accountId, characterId, spUser);
    if (!spCharacter)
    {
        m_packetSender.SendStageMoveRes(accountId, EResultCode::Fail, "character load failed", targetStageDataKey);
        UserPtr removed; m_safeUsers.EraseAndGet(accountId, removed);
        co_return;
    }

    // ── 대상 Stage 해석 (정적 스테이지는 dataKey당 정확히 1개) ──
    std::vector<StagePtr> targets = m_stageManager.FindStagesByDataKey(targetStageDataKey);
    if (targets.size() != 1)
    {
        LOG_WRITE(LogLevel::Error, std::format("reroute - target stage resolve failed. accountId={} stageKey={} count={}",
            accountId, targetStageDataKey, targets.size()));
        m_packetSender.SendStageMoveRes(accountId, EResultCode::Fail, "target stage not found", targetStageDataKey);
        UserPtr removed; m_safeUsers.EraseAndGet(accountId, removed);
        co_return;
    }
    StagePtr spTarget = targets[0];

    // ── 2단계 입장 시작 (로컬 이동과 동일). 도착 위치타입 보관 + Moving 전환 → 유저만 입장 ──
    spUser->SetPendingPositionType(positionType);
    spUser->SetStageState(EUserStageState::Moving);
    spTarget->EnqueueMessage(StageMsg_UserEnter{spUser});

    // ── 클라에 StageMoveRes(성공) → 클라가 맵 로딩 시작 → StageLoadCompleteReq → 대상 Stage가 스폰 ──
    m_packetSender.SendStageMoveRes(accountId, EResultCode::Success, "", targetStageDataKey);

    LOG_WRITE(LogLevel::Info, std::format("reroute - user entering target stage. accountId={} stageId={} stageKey={}",
        accountId, spTarget->GetStageId(), targetStageDataKey));
}

// [개발/테스트] Stage에서 시작하는 코루틴 절차 검증용 (치트 savechar).
// 캐릭터 현재 상태를 DB에 저장(saveCharacterToDB)하면서, 후속작업이 같은 Stage 스레드에서 재개되는지 +
// AsyncPin 카운터 증감을 로그로 확인한다.
// 기대 로그: start(stageBusy=true,charPending=true) → resumed(sameAsLaunch=true,saved=true) → pin released(둘 다 false).
// sameAsLaunch=true 가 "후속작업이 IOCP가 아니라 같은 Stage 스레드에서 재개됨"을 증명한다.
db::DetachedCoTask GameServer::SaveCharacterFromStage(Stage* pStage, CharacterPtr spChar)
{
    const size_t launchTid    = std::hash<std::thread::id>{}(std::this_thread::get_id());
    const int64  characterId  = spChar->GetProto().character_id();

    {
        // 코루틴 prologue(첫 co_await 이전, Stage 스레드)에서 핀 획득 → 카운터 ++.
        AsyncPin pin = pStage->PinForAsync(spChar);

        LOG_WRITE(LogLevel::Info, std::format(
            "[savechar] start. thread={} stageId={} characterId={} stageBusy={} charPending={}",
            launchTid, pStage->GetStageId(), characterId, pStage->HasInFlightAsync(), spChar->HasPendingAsync()));

        // 실제 저장 코루틴 사용. Stage의 resume executor를 넘겨 후속작업이 이 Stage 스레드에서 재개되게 한다.
        // (GetCoroutineResumeExecutor였다면 IOCP에서 재개되어 thread가 달라진다.)
        const bool saved = co_await saveCharacterToDB(spChar, pStage->GetResumeExecutor());

        const size_t resumeTid = std::hash<std::thread::id>{}(std::this_thread::get_id());
        LOG_WRITE(LogLevel::Info, std::format(
            "[savechar] resumed. resumeThread={} sameAsLaunch={} saved={} stageBusy={} charPending={}",
            resumeTid, (resumeTid == launchTid), saved, pStage->HasInFlightAsync(), spChar->HasPendingAsync()));
    }   // AsyncPin 소멸(Stage 스레드) → 카운터 --.

    LOG_WRITE(LogLevel::Info, std::format(
        "[savechar] pin released. stageBusy={} charPending={}",
        pStage->HasInFlightAsync(), spChar->HasPendingAsync()));
}

// [개발/테스트] Currency/Item 테이블 upsert 검증용. (Stage::handleEventAreaEnterReq 가 발사)
db::DetachedCoTask GameServer::UpsertTestCurrencyAndItemFromStage(Stage* pStage, CharacterPtr spChar)
{
    // 코루틴 prologue(첫 co_await 이전, Stage 스레드)에서 핀 획득 → 후속작업까지 캐릭터 수명 보장.
    AsyncPin pin = pStage->PinForAsync(spChar);

    const DataStructures::Character& cproto = spChar->GetProto();
    const int64 characterId = cproto.character_id();
    const int64 accountId   = cproto.owner_account_id();

    UserPtr spUser = spChar->GetUserWeak().lock();
    if (!spUser)
    {
        LOG_WRITE(LogLevel::Error, std::format("[testupsert] owner user gone. characterId={}", characterId));
        co_return;
    }
    const int32 gameDbIndex = spUser->GetGameDbIndex();
    if (!GetDB().HasDatabase(db::EDBType::Game, gameDbIndex))
    {
        LOG_WRITE(LogLevel::Error, std::format("[testupsert] invalid shard. accountId={} idx={}", accountId, gameDbIndex));
        co_return;
    }

    // ── 임의 테스트 데이터 구성 (Stage 스레드). proto는 shared_ptr 로(private copy) ──
    auto spCurrency = std::make_shared<DataStructures::Currency>();
    spCurrency->set_gold(12345);

    auto spAccountCurrency = std::make_shared<DataStructures::AccountCurrency>();
    spAccountCurrency->set_dia(555);

    auto spItem = std::make_shared<DataStructures::Item>();
    const int64 itemId = GenerateObjectId();   // snowflake, 전역 유일 → 매 진입마다 새 아이템 1행
    spItem->set_item_id(itemId);
    spItem->set_item_key(1001);
    spItem->set_item_type(1);
    spItem->set_grade(3);
    spItem->set_count(5);

    // ── 배치에 여러 테이블을 담아 한 트랜잭션으로 저장(퍼시스턴스 레이어) ──
    //    고정 시그니처: 항상 (proto, accountId, characterId). 키 조립은 각 테이블이 알아서 한다.
    auto spBatch = std::make_shared<db::DbSaveBatch>();
    spBatch->Upsert(spCurrency, accountId, characterId);
    spBatch->Upsert(spItem, accountId, characterId);
    spBatch->Upsert(spAccountCurrency, accountId, characterId);

    db::DBResult txResult = co_await db::DbSaveExecutor::Save(
        GetDB(), spBatch, gameDbIndex, pStage->GetResumeExecutor());

    if (!txResult.success)
    {
        LOG_WRITE(LogLevel::Error, std::format("[testupsert] tx failed. accountId={} characterId={} errorCode={} err={}",
            accountId, characterId, txResult.errorCode, txResult.errorMsg));
        co_return;
    }

    LOG_WRITE(LogLevel::Info, std::format("[testupsert] ok. accountId={} characterId={} itemId={}",
        accountId, characterId, itemId));
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
// → 사이드카에서 accountId 추출 → 해당 유저의 패킷 큐에 push
// 유저의 Stage Update 시 OnUserPacket으로 처리됨.
void GameServer::handleRelayedClientPacket(const netlib::PacketPtr& spPacket)
{
    if (!spPacket->HasSidecar())
        return;

    // 사이드카 크기 검증 (게이트웨이는 int64 accountId 8바이트를 붙이기로 약속)
    if (spPacket->GetSidecarSize() != sizeof(int64))
    {
        LOG_WRITE(LogLevel::Warn, std::format("unexpected sidecar size. expected={} actual={} packetType={}",
            sizeof(int64), spPacket->GetSidecarSize(), spPacket->GetHeader()->type));
        return;
    }

    // accountId 추출
    int64 accountId = 0;
    std::memcpy(&accountId, spPacket->GetSidecarData(), sizeof(int64));

    // 유저 조회
    UserPtr spUser;
    if (!m_safeUsers.Find(accountId, spUser) || !spUser)
    {
        LOG_WRITE(LogLevel::Warn, std::format("relayed client packet for unknown user. accountId={} packetType={}",
            accountId, spPacket->GetHeader()->type));
        return;
    }

    // 캐릭터 선택/생성 단계 패킷은 GameServer가 직접 처리 (DB 코루틴 필요).
    // 그외 게임 플레이 패킷은 User 패킷큐에 push → Stage가 처리.
    const uint16 packetType = spPacket->GetHeader()->type;

    // [치트] 수신 패킷 로깅. name 모드는 여기서 이름만 1줄(모든 수신 커버). detail 모드는
    // 타입이 살아나는 지점(아래 case들 / Stage::deserializeUserPacket)에서 이름+JSON 1줄로 찍는다.
    const EPacketLogMode logMode = packetlog::EffectiveMode(*spUser);
    if (logMode == EPacketLogMode::Name)
        packetlog::LogPacket("C->S", accountId, packetType, nullptr);

    switch (packetType)
    {
    case Common::GAME_PACKET_ID_CHARACTER_CREATE_REQ:
    {
        GamePacket::CharacterCreateReq req;
        if (!DeserializePacket(*spPacket, req))
        {
            LOG_WRITE(LogLevel::Warn, std::format("failed to deserialize CharacterCreateReq. accountId={}", accountId));
            return;
        }
        if (logMode == EPacketLogMode::Detail)
            packetlog::LogPacket("C->S", accountId, packetType, &req);
        handleClientCharacterCreate(accountId, std::move(req));
        return;
    }
    case Common::GAME_PACKET_ID_CHARACTER_SELECT_REQ:
    {
        GamePacket::CharacterSelectReq req;
        if (!DeserializePacket(*spPacket, req))
        {
            LOG_WRITE(LogLevel::Warn, std::format("failed to deserialize CharacterSelectReq. accountId={}", accountId));
            return;
        }
        if (logMode == EPacketLogMode::Detail)
            packetlog::LogPacket("C->S", accountId, packetType, &req);
        handleClientCharacterSelect(accountId, std::move(req));
        return;
    }
    default:
        break;
    }

    // 게임 플레이 패킷: User 패킷 큐에 push. 유저가 속한 Stage가 다음 tick에 drain하여 처리.
    spUser->EnqueuePacket(spPacket);
}
