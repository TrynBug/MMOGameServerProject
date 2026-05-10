#include "pch.h"
#include "RegistryServer.h"

#include "ProtoSerializer.h"
#include "Generated/Common/packet_id.pb.h"
#include "Generated/ServerPacket/server_registry_packet.pb.h"

bool RegistryServer::OnInitialize()
{
    // 패킷ID와 핸들러 등록
    m_packetDispatcher.Register<ServerPacket::RegistryRegisterReq>(Common::SERVER_PACKET_ID_REGISTRY_REGISTER_REQ,
        [this](const netlib::ISessionPtr& spSession, const ServerPacket::RegistryRegisterReq& msg) { handleRegisterReq(spSession, msg); });

    m_packetDispatcher.Register<ServerPacket::RegistryHeartbeatRes>(Common::SERVER_PACKET_ID_REGISTRY_HEARTBEAT_RES,
        [this](const netlib::ISessionPtr& spSession, const ServerPacket::RegistryHeartbeatRes& msg) { handleHeartbeatRes(spSession, msg); });

    m_packetDispatcher.Register<ServerPacket::RegistryPollReq>(Common::SERVER_PACKET_ID_REGISTRY_POLL_REQ,
        [this](const netlib::ISessionPtr& spSession, const ServerPacket::RegistryPollReq& msg) { handlePollReq(spSession, msg); });

    m_packetDispatcher.Register<ServerPacket::RegistryUserCountNtf>(Common::SERVER_PACKET_ID_REGISTRY_USER_COUNT_NTF,
        [this](const netlib::ISessionPtr& spSession, const ServerPacket::RegistryUserCountNtf& msg) { handleUserCountNtf(spSession, msg); });

    m_packetDispatcher.Register<ServerPacket::RegistryShutdownReq>(Common::SERVER_PACKET_ID_REGISTRY_SHUTDOWN_REQ,
        [this](const netlib::ISessionPtr& spSession, const ServerPacket::RegistryShutdownReq& msg) { handleShutdownReq(spSession, msg); });

    m_packetDispatcher.SetUnknownPacketHandler([this](const netlib::ISessionPtr& spSession, const netlib::PacketPtr& spPacket)
    {
        LOG_WRITE(LogLevel::Warn, "RegistryServer: unknown packetId=" + std::to_string(spPacket->GetHeader()->type) + " sessionId=" + std::to_string(spSession->GetId()));
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

bool RegistryServer::onAccept(const netlib::ISessionPtr& spSession )
{
    // 레지스트리 서버는 모든 서버의 접속을 허용한다.
    return true;
}

void RegistryServer::onConnect(const netlib::ISessionPtr& spSession)
{
    // 아직 등록 요청 전이므로 세션만 기록해둔다.
    // ServerEntry는 RegistryRegisterReq 수신 후 생성한다.
    LOG_WRITE(LogLevel::Info, "RegistryServer: server connected. sessionId=" + std::to_string(spSession->GetId()));
}

void RegistryServer::onDisconnect(const netlib::ISessionPtr& spSession)
{
    int64 sessionId = spSession->GetId();

    // sessionId로 serverId 조회
    int32 serverId = 0;
    {
        std::unique_lock<std::shared_mutex> lock(m_sessionIndexMutex);
        auto iter = m_sessionToServerId.find(sessionId);
        if (iter == m_sessionToServerId.end())
        {
            // 등록 완료 전에 끊긴 경우 (RegisterReq 미수신)
            LOG_WRITE(LogLevel::Warn, "RegistryServer: unregistered server disconnected. sessionId=" + std::to_string(sessionId));
            return;
        }
        serverId = iter->second;
        m_sessionToServerId.erase(iter);
    }

    // 상태를 Disconnected로 변경하고 세션 참조 해제
    {
        std::unique_lock<std::shared_mutex> lock(m_serverEntriesMutex);
        auto iter = m_serverEntries.find(serverId);
        if (iter == m_serverEntries.end())
            return;

        iter->second.status = ServerStatus::Disconnected;
        iter->second.spSession = nullptr;

        LOG_WRITE(LogLevel::Warn, "RegistryServer: server disconnected. serverId=" + std::to_string(serverId) + " type=" + std::to_string(static_cast<int>(iter->second.serverType)));

        // 연결 끊김을 다른 모든 서버에 브로드캐스트
        broadcastServerInfo(iter->second);
    }
}

// 서버 등록 요청 처리
void RegistryServer::handleRegisterReq(const netlib::ISessionPtr& spSession, const ServerPacket::RegistryRegisterReq& req)
{
    int32 serverId = req.server_id();
    ServerType type = static_cast<ServerType>(req.server_type());
    std::string ip = req.ip();
    uint16 port = static_cast<uint16>(req.port());

    if (!spSession)
    {
        LOG_WRITE(LogLevel::Error, std::format("Session is null. serverId={}, serverType={}, ip={}, port={}", serverId, type, ip, port));
        return;
    }

    // 충돌 검증
    std::string errorMsg;
    if (!validateRegistration(serverId, type, ip, port, errorMsg))
    {
        LOG_WRITE(LogLevel::Error, "RegistryServer: registration rejected. serverId=" + std::to_string(serverId) + " reason=" + errorMsg);

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
    serverEntry.port = port;
    serverEntry.spSession = spSession;
    serverEntry.lastHeartbeatTime = std::chrono::steady_clock::now();

    {
        std::unique_lock<std::shared_mutex> lock(m_serverEntriesMutex);
        m_serverEntries[serverId] = serverEntry;
    }

    // sessionId -> serverId 인덱스 등록
    {
        std::unique_lock<std::shared_mutex> lock(m_sessionIndexMutex);
        m_sessionToServerId[spSession->GetId()] = serverId;
    }

    LOG_WRITE(LogLevel::Info, "RegistryServer: server registered."
             + std::string(" serverId=") + std::to_string(serverId)
             + " type=" + std::to_string(static_cast<int>(type))
             + " ip=" + ip + ":" + std::to_string(port));

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
    {
        std::shared_lock<std::shared_mutex> lock(m_serverEntriesMutex);
        for (const auto& [id, entry] : m_serverEntries)
        {
            if (id == serverId)
                continue;

            if (entry.status == ServerStatus::Disconnected)
                continue;

            sendServerInfoNtf(spSession, entry);
        }
    }

    // 기존 서버들에게 새로등록된 서버정보 브로드캐스트
    broadcastServerInfo(serverEntry);
}

// 하트비트 응답 처리
void RegistryServer::handleHeartbeatRes(const netlib::ISessionPtr& spSession, const ServerPacket::RegistryHeartbeatRes& msg)
{
    if (!spSession)
    {
        LOG_WRITE(LogLevel::Error, "Session is null");
        return;
    }

    int32 serverId = findServerIdBySessionId(spSession->GetId());
    if (serverId == 0)
        return;

    std::unique_lock<std::shared_mutex> lock(m_serverEntriesMutex);
    auto it = m_serverEntries.find(serverId);
    if (it != m_serverEntries.end())
        it->second.lastHeartbeatTime = std::chrono::steady_clock::now();
}

// 서버 목록 폴링 요청 처리
void RegistryServer::handlePollReq(const netlib::ISessionPtr& spSession, const ServerPacket::RegistryPollReq& req)
{
    if (!spSession)
    {
        LOG_WRITE(LogLevel::Error, "Session is null");
        return;
    }

    // 요청한 타입 목록으로 필터링
    std::unordered_map<int, bool> targetTypes;
    for (int i = 0; i < req.target_types_size(); ++i)
        targetTypes[req.target_types(i)] = true;

    ServerPacket::RegistryPollRes res;
    {
        std::shared_lock<std::shared_mutex> lock(m_serverEntriesMutex);
        for (const auto& [id, serverEntry] : m_serverEntries)
        {
            if (serverEntry.status == ServerStatus::Disconnected)
                continue;

            if (!targetTypes.empty())
            {
                if(targetTypes.contains(static_cast<int>(serverEntry.serverType)) == false)
					continue;
            }
                
            ServerPacket::ServerInfoMsg* pInfo = res.add_servers();
            pInfo->set_server_id(serverEntry.serverId);
            pInfo->set_server_type(static_cast<ServerPacket::ServerType>(serverEntry.serverType));
            pInfo->set_status(static_cast<ServerPacket::ServerStatus>(serverEntry.status));
            pInfo->set_ip(serverEntry.ip);
            pInfo->set_port(serverEntry.port);
            pInfo->set_user_count(serverEntry.userCount);
        }
    }

    auto spResPacket = SerializePacket(Common::SERVER_PACKET_ID_REGISTRY_POLL_RES, res);
    if (spResPacket)
        spSession->Send(spResPacket);
}

// 유저수 변경통지 처리
void RegistryServer::handleUserCountNtf(const netlib::ISessionPtr& spSession, const ServerPacket::RegistryUserCountNtf& ntf)
{
    if (!spSession)
    {
        LOG_WRITE(LogLevel::Error, "Session is null");
        return;
    }

    int32 serverId = findServerIdBySessionId(spSession->GetId());
    if (serverId == 0)
        return;

    std::unique_lock<std::shared_mutex> lock(m_serverEntriesMutex);
    auto iter = m_serverEntries.find(serverId);
    if (iter != m_serverEntries.end())
        iter->second.userCount = ntf.user_count();
}

// 서버 종료 요청 처리
void RegistryServer::handleShutdownReq(const netlib::ISessionPtr& spSession, const ServerPacket::RegistryShutdownReq& msg)
{
    if (!spSession)
    {
        LOG_WRITE(LogLevel::Error, "Session is null");
        return;
    }

    int32 serverId = findServerIdBySessionId(spSession->GetId());
    if (serverId == 0)
        return;

    std::unique_lock<std::shared_mutex> lock(m_serverEntriesMutex);
    auto iter = m_serverEntries.find(serverId);
    if (iter == m_serverEntries.end())
        return;

    // 서버 상태 변경
    iter->second.status = ServerStatus::ShuttingDown;

    LOG_WRITE(LogLevel::Info, "RegistryServer: server shutdown requested. serverId=" + std::to_string(serverId)
             + " type=" + std::to_string(static_cast<int>(iter->second.serverType)));

    // 서버상태변경을 다른 서버들에게 브로드캐스트
    broadcastServerInfo(iter->second);
}


// 서버 등록 검증
bool RegistryServer::validateRegistration(int32 serverId, ServerType type, const std::string& ip, uint16 port, std::string& outErrorMsg)
{
    std::string endpoint = std::to_string(static_cast<int>(type)) + ":" + ip + ":" + std::to_string(port);

    std::lock_guard<std::mutex> lock(m_registrationMutex);

    // 동일 serverId가 이미 등록되어 있는지 확인
    auto iterId = m_idToEndpoint.find(serverId);
    if (iterId != m_idToEndpoint.end() && iterId->second != endpoint)
    {
        outErrorMsg = "serverId=" + std::to_string(serverId) + " is already registered with different endpoint: " + iterId->second;
        return false;
    }

    // 동일 IP:Port가 이미 등록되어 있는지 확인
    auto iterEp = m_endpointToId.find(endpoint);
    if (iterEp != m_endpointToId.end() && iterEp->second != serverId)
    {
        outErrorMsg = "endpoint=" + endpoint + " is already registered with different serverId=" + std::to_string(iterEp->second);
        return false;
    }

    // 검증 통과
    m_idToEndpoint[serverId] = endpoint;
    m_endpointToId[endpoint] = serverId;

    // TODO: RegistryDB에 저장
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
    pInfo->set_port(serverEntry.port);
    pInfo->set_user_count(serverEntry.userCount);

    auto spPacket = SerializePacket(Common::SERVER_PACKET_ID_REGISTRY_SERVER_INFO_NTF, ntf);

    if (!spPacket)
    {
        LOG_WRITE(LogLevel::Error, "RegistryServer::broadcastServerInfo - serialize failed");
        return;
    }

    // 자기 자신을 제외한 연결된 모든 서버에 전송
    std::shared_lock<std::shared_mutex> lock(m_serverEntriesMutex);
    for (const auto& [id, entry] : m_serverEntries)
    {
        if (id == serverEntry.serverId)
            continue;

        if (!entry.spSession)
            continue;

        entry.spSession->Send(spPacket);
    }
}

void RegistryServer::sendServerInfoNtf(netlib::ISessionPtr spSession, const ServerEntry& serverEntry)
{
    ServerPacket::RegistryServerInfoNtf ntf;
    ServerPacket::ServerInfoMsg* pInfo = ntf.mutable_server_info();
    pInfo->set_server_id(serverEntry.serverId);
    pInfo->set_server_type(static_cast<ServerPacket::ServerType>(serverEntry.serverType));
    pInfo->set_status(static_cast<ServerPacket::ServerStatus>(serverEntry.status));
    pInfo->set_ip(serverEntry.ip);
    pInfo->set_port(serverEntry.port);
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

    std::shared_lock<std::shared_mutex> lock(m_serverEntriesMutex);
    for (const auto& [id, serverEntry] : m_serverEntries)
    {
        if (serverEntry.spSession)
            serverEntry.spSession->Send(spPacket);
    }
}

void RegistryServer::checkHeartbeatTimeout()
{
    auto now = std::chrono::steady_clock::now();

    // 하트비트 타임아웃된 서버 목록 수집
    std::vector<int32> timedOutIds;
    {
        std::shared_lock<std::shared_mutex> lock(m_serverEntriesMutex);
        for (const auto& [id, serverEntry] : m_serverEntries)
        {
            if (!serverEntry.spSession)
                continue;

            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - serverEntry.lastHeartbeatTime).count();

            if (elapsed >= k_heartbeatTimeoutMs)
                timedOutIds.push_back(id);
        }
    }

    // 타임아웃된 서버 연결 끊기
    // onDisconnect 콜백이 자동으로 상태 업데이트 및 브로드캐스트 처리
    for (int32 serverId : timedOutIds)
    {
        netlib::ISessionPtr spSession;
        {
            std::shared_lock<std::shared_mutex> lock(m_serverEntriesMutex);
            auto iter = m_serverEntries.find(serverId);
            if (iter != m_serverEntries.end())
                spSession = iter->second.spSession;
        }

        if (spSession)
        {
            LOG_WRITE(LogLevel::Warn, "RegistryServer: heartbeat timeout. serverId=" + std::to_string(serverId) + " disconnecting...");
            spSession->Disconnect();
        }
    }
}


int32 RegistryServer::findServerIdBySessionId(int64 sessionId)
{
    std::shared_lock<std::shared_mutex> lock(m_sessionIndexMutex);
    auto iter = m_sessionToServerId.find(sessionId);
    if(iter != m_sessionToServerId.end())
		return iter->second;

    return 0;
}
