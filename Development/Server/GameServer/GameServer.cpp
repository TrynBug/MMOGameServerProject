#include "pch.h"
#include "GameServer.h"
#include "StageObjects/Character.h"

// ── [치트] 패킷 로깅 헬퍼 정의 (선언은 GameServerDefine.h) ──────────────
namespace packetlog
{
    EPacketLogMode EffectiveMode(const User& user)
    {
        const EPacketLogMode userMode   = user.GetCheatPacketLogMode();
        const EPacketLogMode globalMode = GameServer::Instance().GetCheatManager().GetGlobalPacketLogMode();
        return (userMode >= globalMode) ? userMode : globalMode;   // None < Name < Detail
    }

    void LogPacket(const char* dir, int64 userId, uint16 packetType, const google::protobuf::Message* pMsg)
    {
        const std::string& name = Common::GamePacketId_Name(static_cast<Common::GamePacketId>(packetType));
        const char* pName = name.empty() ? "UNKNOWN" : name.c_str();

        if (pMsg != nullptr)
        {
            std::string json;
            packet::ProtoJsonSerializer::ToJson(*pMsg, json);
            LOG_WRITE(LogLevel::Info, std::format("[PKT][{}] uid={} {}({}) {}", dir, userId, pName, packetType, json));
        }
        else
        {
            LOG_WRITE(LogLevel::Info, std::format("[PKT][{}] uid={} {}({})", dir, userId, pName, packetType));
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

    // ── GameDB 열기 ────────────────────────────────────────────
    // 경로가 비어있으면 sqlite3_open 은 성공하지만 "버려지는 임시 DB"를 연다(연결 종료 시 삭제).
    // 그러면 영속성/공유가 깨진 채 무증상으로 동작하므로, 빈 경로는 설정 누락으로 보고 fail-fast 한다.
    // (GameDB 경로는 GameServer.ini 의 [Database]GameDBPath 로 반드시 지정해야 한다. 기본값 없음.)
    if (m_gameDBPath.empty())
    {
        LOG_WRITE(LogLevel::Error, "GameDBPath is empty. Set [Database]GameDBPath in GameServer.ini.");
        return false;
    }

    if (!m_dbQueue.Open(m_gameDBPath, 1))
    {
        LOG_WRITE(LogLevel::Error, std::format("failed to open GameDB at {}", m_gameDBPath));
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

// (user_id, character_id)로 DB에서 캐릭터 row를 읽어 JSON 파싱 후 Character 객체를 생성하여 User에 소유 연결한다.
// User가 강한 소유자(shared_ptr), Character→User는 약참조(weak_ptr).
// 조회 실패/없음/파싱 실패/소유자 불일치/Initialize 실패 시 nullptr(사유는 내부 로그).
// 호출자(캐릭터 선택 / 크로스서버 이동 입장)는 각자 프로토콜에 맞는 실패 응답을 처리한다.
db::AwaitableCoTask<CharacterPtr> GameServer::loadCharacterForUser(int64 userId, int64 characterId, UserPtr spUser)
{
    // ── DB에서 캐릭터 조회 ──
    db::DBResult result = co_await m_dbQueue.ExecuteAsync(
        "SELECT data FROM Characters WHERE user_id = ? AND character_id = ?",
        { userId, characterId },
        GetCoroutineResumeExecutor()
    );

    if (!result.success || result.IsEmpty())
    {
        LOG_WRITE(LogLevel::Warn, std::format("loadCharacter - select failed/empty. userId={} characterId={} success={}",
            userId, characterId, result.success));
        co_return nullptr;
    }

    // ── JSON 파싱 ──
    DataStructures::Character protoData;
    if (!packet::ProtoJsonSerializer::FromJson(result.GetString(0, "data"), protoData))
    {
        LOG_WRITE(LogLevel::Error, std::format("loadCharacter - parse failed. userId={} characterId={}", userId, characterId));
        co_return nullptr;
    }

    // owner_user_id 검증 (PK가 (user_id, character_id) 이므로 통상 통과. DB 손상 방어).
    if (protoData.owner_user_id() != userId)
    {
        LOG_WRITE(LogLevel::Error, std::format("loadCharacter - owner mismatch. userId={} owner={} characterId={}",
            userId, protoData.owner_user_id(), characterId));
        co_return nullptr;
    }

    // ── Character 객체 생성 + User 소유 연결 ──
    CharacterPtr spCharacter = std::make_shared<Character>();
    if (!spCharacter->Initialize(protoData))
    {
        LOG_WRITE(LogLevel::Error, std::format("loadCharacter - Initialize failed. userId={} characterId={}", userId, characterId));
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
db::DetachedCoTask GameServer::handleClientCharacterSelect(int64 userId, GamePacket::CharacterSelectReq req)
{
    const int64 characterId = req.character_id();
    LOG_WRITE(LogLevel::Info, std::format("CharacterSelectReq received. userId={} characterId={}", userId, characterId));

    // ── 1) User 조회 + 상태 검증 (DB 조회 전에 값싼 검증 먼저) ──
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

    // ── 2) DB에서 캐릭터 로드 + Character 객체 생성 (loadCharacterForUser 공통 사용) ──
    // User가 강한 소유자가 된다 (선택~로그아웃까지 유지). Stage는 스폰되어 있는 동안만 함께 보관.
    CharacterPtr spCharacter = co_await loadCharacterForUser(userId, characterId, spUser);
    if (!spCharacter)
    {
        sendCharacterSelectRes(userId, EResultCode::Fail, "character load failed", nullptr, 0);
        co_return;
    }

    // 클라 응답(CharacterSelectRes)/로깅에 실을 캐릭터 데이터는 생성된 객체에서 되찾는다.
    const DataStructures::Character& character = spCharacter->GetProto();
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

// ──────────────────────────────────────────────────────────────
// 크로스서버 이동 (DB 경유)
// ──────────────────────────────────────────────────────────────
//
// 흐름:
//   출발 서버 A: Stage::handleStageMoveReq(크로스서버 분기) → OnUserLeave → BeginCrossServerMove
//                → 캐릭터 DB 저장 → 게이트웨이에 UserMoveToGameServerReq → A에서 유저 제거
//   게이트웨이:  routedGameServerId = B 로 변경 → B에 GatewayUserRerouteNtf
//   목적지 서버 B: handleGatewayUserReroute → DB 로드 → User/Character 생성 → 대상 Stage 입장(Moving)
//                → 클라에 StageMoveRes(성공)
//   클라:        StageMoveRes(성공) → 맵 로딩 → StageLoadCompleteReq(게이트웨이가 B로 라우팅)
//                → B의 Stage가 스폰 + StageLoadCompleteRes (로컬 이동과 동일)
//
// HP/MP/버프/쿨다운 등 휘발성 상태는 현재 Character proto에 저장되지 않으므로 이동 시 직업기본 최대치로
// 리셋된다(캐릭터 재선택과 동일). v1 한정. 보존하려면 proto에 필드를 추가해야 한다.

// 캐릭터의 런타임 상태를 proto에 동기화한 뒤 JSON으로 직렬화하여 DB에 저장(UPDATE)한다.
// user_id/character_id 는 캐릭터 proto에서 얻는다. 직렬화 실패/DB 실패 시 false(사유는 내부 로그).
db::AwaitableCoTask<bool> GameServer::saveCharacterToDB(CharacterPtr spCharacter, db::IResumeExecutor* pResumeExecutor)
{
    // 런타임 좌표/yaw 등을 proto에 반영한 뒤 직렬화.
    spCharacter->SyncRuntimeToProto();
    const DataStructures::Character& proto = spCharacter->GetProto();
    const int64 userId      = proto.owner_user_id();
    const int64 characterId = proto.character_id();

    std::string dataJson;
    if (!packet::ProtoJsonSerializer::ToJson(proto, dataJson))
    {
        LOG_WRITE(LogLevel::Error, std::format("saveCharacter - serialize failed. userId={} characterId={}", userId, characterId));
        co_return false;
    }

    db::DBResult result = co_await m_dbQueue.ExecuteAsync(
        "UPDATE Characters SET data = ? WHERE user_id = ? AND character_id = ?",
        { dataJson, userId, characterId },
        pResumeExecutor
    );

    if (!result.success)
    {
        LOG_WRITE(LogLevel::Error, std::format("saveCharacter - DB update failed. userId={} characterId={} err={}",
            userId, characterId, result.errorMsg));
        co_return false;
    }

    co_return true;
}

// 출발 서버: 크로스서버 이동 개시. 호출 시점에 캐릭터는 이미 현재 Stage에서 빠진(OnUserLeave) 상태여야 한다.
// pResumeExecutor: DB await 후속작업을 재개할 executor(호출한 Stage의 컨텐츠 스레드). 이후 코드가 그 스레드에서 실행된다.
db::DetachedCoTask GameServer::BeginCrossServerMove(int64 userId, int32 targetGameServerId, int32 targetStageDataKey, int32 positionType,
                                                    db::IResumeExecutor* pResumeExecutor)
{
    UserPtr spUser;
    if (!m_safeUsers.Find(userId, spUser) || !spUser)
    {
        LOG_WRITE(LogLevel::Warn, std::format("CrossServerMove - user not found. userId={}", userId));
        co_return;
    }

    CharacterPtr spCharacter = spUser->GetCurrentCharacter();
    if (!spCharacter)
    {
        LOG_WRITE(LogLevel::Error, std::format("CrossServerMove - no character. userId={}", userId));
        co_return;
    }

    const int64 characterId = spCharacter->GetProto().character_id();

    // ── 1) 캐릭터를 DB에 저장 (saveCharacterToDB 공통 사용) ──
    // 후속작업이 호출한 Stage의 컨텐츠 스레드에서 재개되도록 그 Stage의 executor를 넘긴다.
    if (!co_await saveCharacterToDB(spCharacter, pResumeExecutor))
    {
        // TODO(crossserver): 저장 실패 시 현재 Stage 재입장으로 롤백. v1은 실패 응답만 보낸다(유저는 무소속 상태로 남음).
        m_packetSender.SendStageMoveRes(userId, EResultCode::Fail, "server error: db save", targetStageDataKey);
        co_return;
    }

    // ── 2) 게이트웨이에 이동 요청 (게이트웨이가 목적지 게임서버로 재라우팅) ──
    ServerPacket::UserMoveToGameServerReq req;
    req.set_user_id(userId);
    req.set_target_game_server_id(targetGameServerId);
    req.set_character_id(characterId);
    req.set_target_stage_data_key(targetStageDataKey);
    req.set_position_type(positionType);

    netlib::PacketPtr spPacket = SerializePacket(Common::SERVER_PACKET_ID_USER_MOVE_TO_GAME_SERVER_REQ, req);
    if (!spPacket || !SendToGateway(spUser->GetGatewayId(), spPacket))
    {
        LOG_WRITE(LogLevel::Error, std::format("CrossServerMove - send to gateway failed. userId={} gatewayId={}", userId, spUser->GetGatewayId()));
        m_packetSender.SendStageMoveRes(userId, EResultCode::Fail, "server error: gateway route", targetStageDataKey);
        co_return;
    }

    // ── 3) 출발 서버에서 유저 제거. 이후 이 유저의 클라 패킷은 게이트웨이가 목적지 서버로 라우팅한다. ──
    UserPtr removed;
    m_safeUsers.EraseAndGet(userId, removed);   // 글로벌 맵에서 제거 → User/Character 파괴

    LOG_WRITE(LogLevel::Info, std::format("CrossServerMove - handed off. userId={} characterId={} -> gameServerId={} stageKey={}",
        userId, characterId, targetGameServerId, targetStageDataKey));
}

// 목적지 서버: 게이트웨이가 재라우팅한 유저를 받아 DB에서 캐릭터를 로드하고 대상 Stage에 입장시킨다.
db::DetachedCoTask GameServer::handleGatewayUserReroute(netlib::ISessionPtr /*spSession*/, ServerPacket::GatewayUserRerouteNtf msg)
{
    const int64       userId             = msg.user_id();
    const int32       gatewayId          = msg.gateway_id();
    const std::string clientIp           = msg.client_ip();
    const int64       characterId        = msg.character_id();
    const int32       targetStageDataKey = msg.target_stage_data_key();
    const auto        positionType       = static_cast<EStagePositionType>(msg.position_type());

    LOG_WRITE(LogLevel::Info, std::format("GatewayUserRerouteNtf received. userId={} characterId={} stageKey={} gatewayId={}",
        userId, characterId, targetStageDataKey, gatewayId));

    if (m_safeUsers.Contains(userId))
    {
        LOG_WRITE(LogLevel::Warn, std::format("reroute - user already exists. userId={}", userId));
        co_return;
    }

    // ── 1) User 생성 + DB에서 캐릭터 로드 + Character 객체 생성 (loadCharacterForUser 공통 사용) ──
    // StageMoveRes 송신은 유저가 글로벌맵(m_safeUsers)에 있어야 게이트웨이를 찾으므로,
    // 실패 시에도 응답을 먼저 보낸 다음 유저를 제거한다.
    UserPtr spUser = std::make_shared<User>(userId, gatewayId, clientIp);
    m_safeUsers.Insert(userId, spUser);

    CharacterPtr spCharacter = co_await loadCharacterForUser(userId, characterId, spUser);
    if (!spCharacter)
    {
        m_packetSender.SendStageMoveRes(userId, EResultCode::Fail, "character load failed", targetStageDataKey);
        UserPtr removed; m_safeUsers.EraseAndGet(userId, removed);
        co_return;
    }

    // ── 2) 대상 Stage 해석 (정적 스테이지는 dataKey당 정확히 1개) ──
    std::vector<StagePtr> targets = m_stageManager.FindStagesByDataKey(targetStageDataKey);
    if (targets.size() != 1)
    {
        LOG_WRITE(LogLevel::Error, std::format("reroute - target stage resolve failed. userId={} stageKey={} count={}",
            userId, targetStageDataKey, targets.size()));
        m_packetSender.SendStageMoveRes(userId, EResultCode::Fail, "target stage not found", targetStageDataKey);
        UserPtr removed; m_safeUsers.EraseAndGet(userId, removed);
        co_return;
    }
    StagePtr spTarget = targets[0];

    // ── 3) 2단계 입장 시작 (로컬 이동과 동일). 도착 위치타입 보관 + Moving 전환 → 유저만 입장 ──
    spUser->SetPendingPositionType(positionType);
    spUser->SetStageState(EUserStageState::Moving);
    spTarget->EnqueueMessage(StageMsg_UserEnter{spUser});

    // ── 4) 클라에 StageMoveRes(성공) → 클라가 맵 로딩 시작 → StageLoadCompleteReq → 대상 Stage가 스폰 ──
    m_packetSender.SendStageMoveRes(userId, EResultCode::Success, "", targetStageDataKey);

    LOG_WRITE(LogLevel::Info, std::format("reroute - user entering target stage. userId={} stageId={} stageKey={}",
        userId, spTarget->GetStageId(), targetStageDataKey));
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

    // [치트] 수신 패킷 로깅. name 모드는 여기서 이름만 1줄(모든 수신 커버). detail 모드는
    // 타입이 살아나는 지점(아래 case들 / Stage::deserializeUserPacket)에서 이름+JSON 1줄로 찍는다.
    const EPacketLogMode logMode = packetlog::EffectiveMode(*spUser);
    if (logMode == EPacketLogMode::Name)
        packetlog::LogPacket("C->S", userId, packetType, nullptr);

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
        if (logMode == EPacketLogMode::Detail)
            packetlog::LogPacket("C->S", userId, packetType, &req);
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
        if (logMode == EPacketLogMode::Detail)
            packetlog::LogPacket("C->S", userId, packetType, &req);
        handleClientCharacterSelect(userId, std::move(req));
        return;
    }
    default:
        break;
    }

    // 게임 플레이 패킷: User 패킷 큐에 push. 유저가 속한 Stage가 다음 tick에 drain하여 처리.
    spUser->EnqueuePacket(spPacket);
}
