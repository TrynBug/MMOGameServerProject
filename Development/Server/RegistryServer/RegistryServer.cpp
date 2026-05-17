#include "pch.h"
#include "RegistryServer.h"

#include "ProtoSerializer.h"
#include "Generated/Common/packet_id.pb.h"
#include "Generated/ServerPacket/server_registry_packet.pb.h"

bool RegistryServer::OnInitialize()
{
    // 패킷ID와 핸들러 등록
    m_packetDispatcher.Register<ServerPacket::RegistryRegisterReq>(Common::SERVER_PACKET_ID_REGISTRY_REGISTER_REQ,
        [this](auto& spSession, auto& msg) { handleRegisterReq(spSession, msg); });

    m_packetDispatcher.Register<ServerPacket::RegistryHeartbeatRes>(Common::SERVER_PACKET_ID_REGISTRY_HEARTBEAT_RES,
        [this](auto& spSession, auto& msg) { handleHeartbeatRes(spSession, msg); });

    m_packetDispatcher.Register<ServerPacket::RegistryPollReq>(Common::SERVER_PACKET_ID_REGISTRY_POLL_REQ,
        [this](auto& spSession, auto& msg) { handlePollReq(spSession, msg); });

    m_packetDispatcher.Register<ServerPacket::RegistryUserCountNtf>(Common::SERVER_PACKET_ID_REGISTRY_USER_COUNT_NTF,
        [this](auto& spSession, auto& msg) { handleUserCountNtf(spSession, msg); });

    m_packetDispatcher.Register<ServerPacket::RegistryShutdownReq>(Common::SERVER_PACKET_ID_REGISTRY_SHUTDOWN_REQ,
        [this](auto& spSession, auto& msg) { handleShutdownReq(spSession, msg); });

    m_packetDispatcher.SetUnknownPacketHandler([this](const netlib::ISessionPtr& spSession, const netlib::PacketPtr& spPacket)
    {
        LOG_WRITE(LogLevel::Warn, std::format("RegistryServer: unknown packetId={} sessionId={}", spPacket->GetHeader()->type, spSession->GetId()));
    });

    // 네트워크 이벤트 핸들러 콜백 등록
    m_listenEventHandler.onAccept = [this](const netlib::ISessionPtr& spSession) { return onAccept(spSession); };
    m_listenEventHandler.onConnect = [this](const netlib::ISessionPtr& spSession) { onConnect(spSession); };
    m_listenEventHandler.onRecv = [this](const netlib::ISessionPtr& spSession, const netlib::PacketPtr& spPacket)
    {
        m_packetDispatcher.Dispatch(spSession, spPacket);
    };
    m_listenEventHandler.onDisconnect = [this](const netlib::ISessionPtr& spSession) { onDisconnect(spSession); };
    m_listenEventHandler.onLog = [this](netlib::LogLevel netLogLevel, const netlib::ISessionPtr& spSession, const std::string& msg)
    {
        const LogLevel logLevel = serverbase::NetLogLevelToLogLevel(netLogLevel);
        LOG_WRITE(logLevel, msg);
    };

    // TODO: RegistryDB에서 기존 serverId 데이터 로드
    // 현재는 메모리만 사용 (DB 연동은 이후 구현)
    LOG_WRITE(LogLevel::Info, "RegistryServer::OnInitialize - DB load skipped (not implemented yet)");

    // 하트비트 전송 타이머 (30초)
    m_heartbeatSendTimerId = GetTimer().Register(k_heartbeatIntervalMs, [this]()
    {
        sendHeartbeatToAll();
    });

    // 하트비트 타임아웃 체크 타이머 (10초마다 체크)
    m_heartbeatCheckTimerId = GetTimer().Register(10000, [this]()
    {
        checkHeartbeatTimeout();
    });

    LOG_WRITE(LogLevel::Info, "RegistryServer::OnInitialize complete");
    return true;
}

void RegistryServer::OnBeforeShutdown()
{
    LOG_WRITE(LogLevel::Info, "RegistryServer::OnBeforeShutdown");
}

bool RegistryServer::onAccept(const netlib::ISessionPtr& spSession)
{
    // accept 시점에 빈 메타정보 설정. 메타정보는 RegisterReq 수신 후 채운다.
    spSession->SetUserData(std::make_shared<ServerSessionMetaInfo>());
    return true;
}

void RegistryServer::onConnect(const netlib::ISessionPtr& spSession)
{
    LOG_WRITE(LogLevel::Info, std::format("RegistryServer: server connected. sessionId={}", spSession->GetId()));
}

void RegistryServer::onDisconnect(const netlib::ISessionPtr& spSession)
{
    ServerSessionMetaInfo* pMeta = getServerSessionMeta(spSession);
    if (!pMeta || pMeta->serverId == 0)
    {
        LOG_WRITE(LogLevel::Warn, std::format("RegistryServer: unregistered server disconnected. sessionId={}", spSession->GetId()));
        return;
    }

    int32 serverId = pMeta->serverId;

    // 상태를 Disconnected로 변경하고 세션 참조 해제
    ServerEntry entry;
    if (!m_safeServerEntries.Find(serverId, entry))
        return;

    entry.status = ServerStatus::Disconnected;
    entry.spSession = nullptr;
    m_safeServerEntries.Insert(serverId, entry);

    LOG_WRITE(LogLevel::Warn, std::format("RegistryServer: server disconnected. serverId={} type={}", serverId, static_cast<int>(entry.serverType)));

    broadcastServerInfo(entry);
}

// 서버 등록 요청 처리
void RegistryServer::handleRegisterReq(const netlib::ISessionPtr& spSession, const ServerPacket::RegistryRegisterReq& req)
{
    int32 serverId = req.server_id();
    ServerType type = static_cast<ServerType>(req.server_type());
    std::string ip = req.ip();
    uint16 clientPort = static_cast<uint16>(req.client_port());
    uint16 internalPort = static_cast<uint16>(req.internal_port());

    if (!spSession)
    {
        LOG_WRITE(LogLevel::Error, std::format("Session is null. serverId={}, serverType={}, ip={}, clientPort={}, internalPort={}", serverId, static_cast<int32>(type), ip, clientPort, internalPort));
        return;
    }

    // 충돌 검증
    std::string errorMsg;
    if (!validateRegistration(serverId, type, ip, internalPort, errorMsg))
    {
        LOG_WRITE(LogLevel::Error, std::format("RegistryServer: registration rejected. serverId={} reason={}", serverId, errorMsg));

        ServerPacket::RegistryRegisterRes res;
        res.set_success(false);
        res.set_server_id(serverId);
        res.set_message(errorMsg);

        auto spPacket = SerializePacket(Common::SERVER_PACKET_ID_REGISTRY_REGISTER_RES, res);
        if (spPacket)
            spSession->Send(spPacket);

        spSession->Disconnect();
        return;
    }

    // ServerEntry 생성 및 등록
    ServerEntry serverEntry;
    serverEntry.serverId = serverId;
    serverEntry.serverType = type;
    serverEntry.status = ServerStatus::Running;
    serverEntry.ip = ip;
    serverEntry.clientPort = clientPort;
    serverEntry.internalPort = internalPort;
    serverEntry.spSession = spSession;
    serverEntry.lastHeartbeatTime = std::chrono::steady_clock::now();

    m_safeServerEntries.Insert(serverId, serverEntry);

    // 세션에 serverId 기록
    ServerSessionMetaInfo* pMeta = getServerSessionMeta(spSession);
    if (pMeta)
        pMeta->serverId = serverId;

    LOG_WRITE(LogLevel::Info, std::format("RegistryServer: server registered. serverId={} type={} ip={}, clientPort={}, internalPort={}", serverId, static_cast<int>(type), ip, clientPort, internalPort));

    // 서버 등록 완료 응답
    {
        ServerPacket::RegistryRegisterRes res;
        res.set_success(true);
        res.set_server_id(serverId);

        auto spPacket = SerializePacket(Common::SERVER_PACKET_ID_REGISTRY_REGISTER_RES, res);
        if (spPacket)
            spSession->Send(spPacket);
    }

    // 새로등록된 서버에게 기존 서버목록 전송
    m_safeServerEntries.ForEach([&](const int32& id, const ServerEntry& entry)
    {
        if (id == serverId || entry.status == ServerStatus::Disconnected)
            return;

        sendServerInfoNtf(spSession, entry);
    });

    // 기존 서버들에게 새로등록된 서버정보 브로드캐스트
    broadcastServerInfo(serverEntry);
}

// 하트비트 응답 처리
void RegistryServer::handleHeartbeatRes(const netlib::ISessionPtr& spSession, const ServerPacket::RegistryHeartbeatRes& msg)
{
    ServerSessionMetaInfo* pMeta = getServerSessionMeta(spSession);
    if (!pMeta || pMeta->serverId == 0)
        return;

    ServerEntry entry;
    if (!m_safeServerEntries.Find(pMeta->serverId, entry))
        return;

    entry.lastHeartbeatTime = std::chrono::steady_clock::now();
    m_safeServerEntries.Insert(pMeta->serverId, entry);
}

// 서버 목록 폴링 요청 처리
void RegistryServer::handlePollReq(const netlib::ISessionPtr& spSession, const ServerPacket::RegistryPollReq& req)
{
    // 요청한 타입 목록으로 필터링
    std::unordered_map<int, bool> targetTypes;
    for (int i = 0; i < req.target_types_size(); ++i)
        targetTypes[req.target_types(i)] = true;

    ServerPacket::RegistryPollRes res;
    m_safeServerEntries.ForEach([&](const int32&, const ServerEntry& serverEntry)
    {
        if (serverEntry.status == ServerStatus::Disconnected)
            return;

        if (!targetTypes.empty() && !targetTypes.contains(static_cast<int>(serverEntry.serverType)))
            return;

        ServerPacket::ServerInfoMsg* pInfo = res.add_servers();
        pInfo->set_server_id(serverEntry.serverId);
        pInfo->set_server_type(static_cast<ServerPacket::ServerType>(serverEntry.serverType));
        pInfo->set_status(static_cast<ServerPacket::ServerStatus>(serverEntry.status));
        pInfo->set_ip(serverEntry.ip);
        pInfo->set_client_port(serverEntry.clientPort);
        pInfo->set_internal_port(serverEntry.internalPort);
        pInfo->set_user_count(serverEntry.userCount);
    });

    auto spResPacket = SerializePacket(Common::SERVER_PACKET_ID_REGISTRY_POLL_RES, res);
    if (spResPacket)
        spSession->Send(spResPacket);
}

// 유저수 변경통지 처리
void RegistryServer::handleUserCountNtf(const netlib::ISessionPtr& spSession, const ServerPacket::RegistryUserCountNtf& ntf)
{
    ServerSessionMetaInfo* pMeta = getServerSessionMeta(spSession);
    if (!pMeta || pMeta->serverId == 0)
        return;

    ServerEntry entry;
    if (!m_safeServerEntries.Find(pMeta->serverId, entry))
        return;

    entry.userCount = ntf.user_count();
    m_safeServerEntries.Insert(pMeta->serverId, entry);
}

// 서버 종료 요청 처리
void RegistryServer::handleShutdownReq(const netlib::ISessionPtr& spSession, const ServerPacket::RegistryShutdownReq& msg)
{
    ServerSessionMetaInfo* pMeta = getServerSessionMeta(spSession);
    if (!pMeta || pMeta->serverId == 0)
        return;

    ServerEntry entry;
    if (!m_safeServerEntries.Find(pMeta->serverId, entry))
        return;

    entry.status = ServerStatus::ShuttingDown;
    m_safeServerEntries.Insert(pMeta->serverId, entry);

    LOG_WRITE(LogLevel::Info, std::format("RegistryServer: server shutdown requested. serverId={} type={}", pMeta->serverId, static_cast<int>(entry.serverType)));

    broadcastServerInfo(entry);
}

// 세션에서 ServerSessionMetaInfo를 꺼낸다.
// 주의: ServerSessionMetaInfo* 를 다른곳에 보관해두면 안됨. 세션이 제거될때 함께 제거되기 때문
ServerSessionMetaInfo* RegistryServer::getServerSessionMeta(const netlib::ISessionPtr& spSession)
{
    return static_cast<ServerSessionMetaInfo*>(spSession->GetUserData().get());
}

// 서버 등록 검증
bool RegistryServer::validateRegistration(int32 serverId, ServerType type, const std::string& ip, uint16 internalPort, std::string& outErrorMsg)
{
    std::string endpoint = std::to_string(static_cast<int>(type)) + ":" + ip + ":" + std::to_string(internalPort);

    // 동일 serverId가 이미 등록되어 있는지 확인
    std::string existingEndpoint;
    if (m_safeIdToEndpoint.Find(serverId, existingEndpoint) && existingEndpoint != endpoint)
    {
        outErrorMsg = std::format("serverId={} is already registered with different endpoint: {}", serverId, existingEndpoint);
        return false;
    }

    // 동일 IP:Port가 이미 등록되어 있는지 확인
    int32 existingId = 0;
    if (m_safeEndpointToId.Find(endpoint, existingId) && existingId != serverId)
    {
        outErrorMsg = std::format("endpoint={} is already registered with different serverId={}", endpoint, existingId);
        return false;
    }

    m_safeIdToEndpoint.Insert(serverId, endpoint);
    m_safeEndpointToId.Insert(endpoint, serverId);

    return true;
}

// 브로드캐스트
void RegistryServer::broadcastServerInfo(const ServerEntry& serverEntry)
{
    ServerPacket::RegistryServerInfoNtf ntf;
    ServerPacket::ServerInfoMsg* pInfo = ntf.mutable_server_info();
    pInfo->set_server_id(serverEntry.serverId);
    pInfo->set_server_type(static_cast<ServerPacket::ServerType>(serverEntry.serverType));
    pInfo->set_status(static_cast<ServerPacket::ServerStatus>(serverEntry.status));
    pInfo->set_ip(serverEntry.ip);
    pInfo->set_client_port(serverEntry.clientPort);
    pInfo->set_internal_port(serverEntry.internalPort);
    pInfo->set_user_count(serverEntry.userCount);

    auto spPacket = SerializePacket(Common::SERVER_PACKET_ID_REGISTRY_SERVER_INFO_NTF, ntf);

    if (!spPacket)
    {
        LOG_WRITE(LogLevel::Error, "RegistryServer::broadcastServerInfo - serialize failed");
        return;
    }

    m_safeServerEntries.ForEach([&](const int32& id, const ServerEntry& entry)
    {
        if (id == serverEntry.serverId || !entry.spSession)
            return;
        entry.spSession->Send(spPacket);
    });
}

void RegistryServer::sendServerInfoNtf(netlib::ISessionPtr spSession, const ServerEntry& serverEntry)
{
    ServerPacket::RegistryServerInfoNtf ntf;
    ServerPacket::ServerInfoMsg* pInfo = ntf.mutable_server_info();
    pInfo->set_server_id(serverEntry.serverId);
    pInfo->set_server_type(static_cast<ServerPacket::ServerType>(serverEntry.serverType));
    pInfo->set_status(static_cast<ServerPacket::ServerStatus>(serverEntry.status));
    pInfo->set_ip(serverEntry.ip);
    pInfo->set_client_port(serverEntry.clientPort);
    pInfo->set_internal_port(serverEntry.internalPort);
    pInfo->set_user_count(serverEntry.userCount);

    auto spPacket = SerializePacket(Common::SERVER_PACKET_ID_REGISTRY_SERVER_INFO_NTF, ntf);
    if (spPacket)
        spSession->Send(spPacket);
}

// 하트비트요청 보내기
void RegistryServer::sendHeartbeatToAll()
{
    ServerPacket::RegistryHeartbeatReq req;
    const int64 nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    req.set_timestamp_ms(nowMs);

    auto spPacket = SerializePacket(Common::SERVER_PACKET_ID_REGISTRY_HEARTBEAT_REQ, req);
    if (!spPacket)
        return;

    m_safeServerEntries.ForEach([&](const int32&, const ServerEntry& serverEntry)
    {
        if (serverEntry.spSession)
            serverEntry.spSession->Send(spPacket);
    });
}

void RegistryServer::checkHeartbeatTimeout()
{
    auto now = std::chrono::steady_clock::now();

    std::vector<int32> timedOutIds = m_safeServerEntries.CollectKeys(
        [now](const int32&, const ServerEntry& entry)
        {
            if (!entry.spSession) return false;
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - entry.lastHeartbeatTime).count();
            return elapsed >= k_heartbeatTimeoutMs;
        });

    for (int32 serverId : timedOutIds)
    {
        ServerEntry entry;
        if (!m_safeServerEntries.Find(serverId, entry))
            continue;

        if (entry.spSession)
        {
            LOG_WRITE(LogLevel::Warn, std::format("RegistryServer: heartbeat timeout. serverId={} disconnecting...", serverId));
            entry.spSession->Disconnect();
        }
    }
}
