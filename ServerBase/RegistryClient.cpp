#include "pch.h"
#include "RegistryClient.h"
#include "Logger.h"

#include "Generated/Common/packet_id.pb.h"
#include "Generated/ServerPacket/server_registry_packet.pb.h"

#include "IoContext.h"
#include "NetClient.h"
#include "ServerBase.h"

namespace serverbase
{

RegistryClient::~RegistryClient()
{
    Stop();
}

bool RegistryClient::Initialize(ServerBase* pServerBase, const Config& config)
{
    m_config = config;
    m_pServerBase = pServerBase;

    if (m_config.myServerId <= 0)
    {
        LOG_WRITE(LogLevel::Error, "RegistryClient::Initialize - invalid serverId: " + std::to_string(m_config.myServerId));
        return false;
    }

    netlib::NetClientConfig clientConfig;
    clientConfig.bUseNagle            = false;
    clientConfig.bAutoReconnect       = true;
    clientConfig.reconnectIntervalMs  = m_config.reconnectMs;

    // NetClient 생성
    m_upNetClient = std::make_unique<netlib::NetClient>(&pServerBase->GetIoContext());
    if (!m_upNetClient->Initialize(clientConfig))
    {
        LOG_WRITE(LogLevel::Error, "RegistryClient: NetClient Initialize failed");
        return false;
    }

    // EventHandler 등록
    m_upNetClient->SetEventHandler(this);

    return true;
}

void RegistryClient::Start()
{
    m_timer.Start();

    m_upNetClient->Connect(m_config.registryIp, m_config.registryPort);
    LOG_WRITE(LogLevel::Info, "RegistryClient: connecting to registry " + m_config.registryIp + ":" + std::to_string(m_config.registryPort));
}

void RegistryClient::Stop()
{
    m_timer.Stop();

    {
        std::lock_guard<std::mutex> lock(m_sessionMutex);
        m_spSession.reset();
    }

    if (m_upNetClient)
    {
        m_upNetClient->Shutdown();
        m_upNetClient.reset();
    }
}

// 다른 서버 정보가 갱신될 때 호출될 콜백 등록
void RegistryClient::SetServerInfoCallback(ServerInfoCallback callback)
{
    m_serverInfoCallback = std::move(callback);
}

// 등록 거부 시 호출될 콜백 등록 (서버 종료 유도용)
void RegistryClient::SetRegisterRejectedCallback(RegisterRejectedCallback callback)
{
    m_registerRejectedCallback = std::move(callback);
}

// ──────────── INetEventHandler ─────────────────────────────

bool RegistryClient::OnAccept(const netlib::ISessionPtr& spSession)
{
    return false;  // NetClient에서는 호출되지 않는다
}

void RegistryClient::OnConnect(const netlib::ISessionPtr& spSession)
{
    {
        std::lock_guard<std::mutex> lock(m_sessionMutex);
        m_spSession = spSession;
    }

    LOG_WRITE(LogLevel::Info, "RegistryClient: connected to registry server. sending register request.");
    sendRegisterReq();
}

void RegistryClient::OnRecv(const netlib::ISessionPtr& spSession, const netlib::PacketPtr& spPacket)
{
    uint16 packetId = spPacket->GetHeader()->type;

    switch (packetId)
    {
        // 서버 등록 요청 응답
    case Common::SERVER_PACKET_ID_REGISTRY_REGISTER_RES:
        handleRegisterRes(*spPacket);
        break;

    case Common::SERVER_PACKET_ID_REGISTRY_SERVER_INFO_NTF:
        handleServerInfoNtf(*spPacket);
        break;

    case Common::SERVER_PACKET_ID_REGISTRY_POLL_RES:
        handlePollRes(*spPacket);
        break;

    case Common::SERVER_PACKET_ID_REGISTRY_HEARTBEAT_REQ:
        handleHeartbeatReq(*spPacket);
        break;

    default:
        LOG_WRITE(LogLevel::Warn, "RegistryClient: unknown packet id=" + std::to_string(packetId));
        break;
    }
}

void RegistryClient::OnDisconnect(const netlib::ISessionPtr& spSession)
{
    LOG_WRITE(LogLevel::Warn, "RegistryClient: disconnected from registry server. will reconnect.");
    m_bRegistered = false;

    {
        std::lock_guard<std::mutex> lock(m_sessionMutex);
        m_spSession.reset();
    }

    if (m_pollTimerId != -1)
    {
        m_timer.Unregister(m_pollTimerId);
        m_pollTimerId = -1;
    }
    if (m_userCountTimerId != -1)
    {
        m_timer.Unregister(m_userCountTimerId);
        m_userCountTimerId = -1;
    }
}

void RegistryClient::OnLog(netlib::LogLevel netLogLevel, const netlib::ISessionPtr& spSession, const std::string& msg)
{
	const LogLevel logLevel = NetLogLevelToLogLevel(netLogLevel);
	LOG_WRITE(logLevel, "RegistryClient: " + msg);
}

// Send
void RegistryClient::sendPacket(netlib::PacketPtr spPacket)
{
    std::lock_guard<std::mutex> lock(m_sessionMutex);
    if (m_spSession)
        m_spSession->Send(spPacket);
}

// 서버 등록 요청 전송
void RegistryClient::sendRegisterReq()
{
    ServerPacket::RegistryRegisterReq req;
    req.set_server_type(static_cast<ServerPacket::ServerType>(m_config.myServerType));
    req.set_server_id(m_config.myServerId);
    req.set_ip(m_config.myIp);
    req.set_port(m_config.myPort);

    auto spPacket = m_pServerBase->SerializePacket(Common::SERVER_PACKET_ID_REGISTRY_REGISTER_REQ, req);

    if (!spPacket)
    {
        LOG_WRITE(LogLevel::Error, "RegistryClient: failed to serialize RegistryRegisterReq");
        return;
    }

    sendPacket(std::move(spPacket));
}

// 서버정보 폴링 요청 전송
void RegistryClient::sendPollReq()
{
    if (!m_bRegistered)
        return;

    ServerPacket::RegistryPollReq req;
    for (ServerType type : m_config.pollTargetTypes)
    {
        req.add_target_types(static_cast<ServerPacket::ServerType>(type));
    }

    auto spPacket = m_pServerBase->SerializePacket(Common::SERVER_PACKET_ID_REGISTRY_POLL_REQ, req);

    if (!spPacket)
    {
        LOG_WRITE(LogLevel::Error, "RegistryClient: failed to serialize RegistryPollReq");
        return;
    }

    sendPacket(std::move(spPacket));
}

// 하트비트 요청에 대한 응답 전송
void RegistryClient::sendHeartbeatRes(int64 timestampMs)
{
    ServerPacket::RegistryHeartbeatRes res;
    res.set_timestamp_ms(timestampMs);

    auto spPacket = m_pServerBase->SerializePacket(Common::SERVER_PACKET_ID_REGISTRY_HEARTBEAT_RES, res);

    if (!spPacket)
    {
        LOG_WRITE(LogLevel::Error, "RegistryClient: failed to serialize RegistryHeartbeatRes");
        return;
    }

    sendPacket(std::move(spPacket));
}

// 접속자 수 보고 전송
void RegistryClient::sendUserCountReport()
{
    if (!m_bRegistered)
        return;

    ServerPacket::RegistryUserCountNtf ntf;
    ntf.set_user_count(m_userCount.load());

    auto spPacket = m_pServerBase->SerializePacket(Common::SERVER_PACKET_ID_REGISTRY_USER_COUNT_NTF, ntf);

    if (!spPacket)
    {
        LOG_WRITE(LogLevel::Error, "RegistryClient: failed to serialize RegistryUserCountNtf");
        return;
    }

    sendPacket(std::move(spPacket));
}

// Graceful Shutdown 알림을 레지스트리 서버로 전송
void RegistryClient::SendShutdownNotify()
{
    if (!m_bRegistered)
        return;

    ServerPacket::RegistryShutdownReq req;
    auto spPacket = m_pServerBase->SerializePacket(Common::SERVER_PACKET_ID_REGISTRY_SHUTDOWN_REQ, req);

    if (!spPacket)
    {
        LOG_WRITE(LogLevel::Error, "RegistryClient: failed to serialize RegistryShutdownReq");
        return;
    }

    sendPacket(std::move(spPacket));
    LOG_WRITE(LogLevel::Info, "RegistryClient: sent shutdown notify to registry server.");
}

// 서버 등록 요청 응답 처리
void RegistryClient::handleRegisterRes(const netlib::Packet& packet)
{
    ServerPacket::RegistryRegisterRes res;
    if (!m_pServerBase->DeserializePacket(packet, res))
    {
        LOG_WRITE(LogLevel::Error, "RegistryClient: failed to deserialize RegistryRegisterRes");
        return;
    }

    if (!res.success())
    {
        // 등록 거부됨(서버ID 충돌 등). 서버는 종료되어야 함
        std::string reason = res.message();
        LOG_WRITE(LogLevel::Error, "RegistryClient: register rejected - " + reason + " (serverId=" + std::to_string(m_config.myServerId) + "). server must shut down.");

        if (m_registerRejectedCallback)
            m_registerRejectedCallback(reason);

        return;
    }

    m_bRegistered  = true;

    LOG_WRITE(LogLevel::Info, "RegistryClient: registered successfully. serverId=" + std::to_string(m_config.myServerId));

    // 서버정보 폴링 타이머 등록
    if (!m_config.pollTargetTypes.empty())
    {
        m_pollTimerId = m_timer.Register(m_config.pollIntervalMs, [this]()
        {
            sendPollReq();
        });
    }

    // 유저 수 보고 타이머 등록
    if (m_config.userCountReportMs > 0)
    {
        m_userCountTimerId = m_timer.Register(m_config.userCountReportMs, [this]()
        {
            sendUserCountReport();
        });
    }
}

// 서버정보 업데이트 패킷 처리
void RegistryClient::handleServerInfoNtf(const netlib::Packet& packet)
{
    ServerPacket::RegistryServerInfoNtf ntf;
    if (!m_pServerBase->DeserializePacket(packet, ntf))
    {
        LOG_WRITE(LogLevel::Error, "RegistryClient: failed to deserialize RegistryServerInfoNtf");
        return;
    }

    const ServerPacket::ServerInfoMsg& msgInfo = ntf.server_info();
    ServerInfo info;
    info.serverId   = msgInfo.server_id();
    info.serverType = static_cast<ServerType>(msgInfo.server_type());
    info.status     = static_cast<ServerStatus>(msgInfo.status());
    info.ip         = msgInfo.ip();
    info.port       = static_cast<uint16>(msgInfo.port());
    info.userCount  = msgInfo.user_count();

    applyServerInfo(info);
}

// 서버정보 폴링 응답 처리
void RegistryClient::handlePollRes(const netlib::Packet& packet)
{
    ServerPacket::RegistryPollRes res;
    if (!m_pServerBase->DeserializePacket(packet, res))
    {
        LOG_WRITE(LogLevel::Error, "RegistryClient: failed to deserialize RegistryPollRes");
        return;
    }

    for (const ServerPacket::ServerInfoMsg& msgInfo : res.servers())
    {
        ServerInfo info;
        info.serverId   = msgInfo.server_id();
        info.serverType = static_cast<ServerType>(msgInfo.server_type());
        info.status     = static_cast<ServerStatus>(msgInfo.status());
        info.ip         = msgInfo.ip();
        info.port       = static_cast<uint16>(msgInfo.port());
        info.userCount  = msgInfo.user_count();

        applyServerInfo(info);
    }
}

// 하트비트 요청 처리
void RegistryClient::handleHeartbeatReq(const netlib::Packet& packet)
{
    ServerPacket::RegistryHeartbeatReq req;
    if (!m_pServerBase->DeserializePacket(packet, req))
    {
        LOG_WRITE(LogLevel::Error, "RegistryClient: failed to deserialize RegistryHeartbeatReq");
        return;
    }

    // 하트비트 응답 보냄
    sendHeartbeatRes(req.timestamp_ms());
}

// 서버 정보 업데이트 적용 내부로직
void RegistryClient::applyServerInfo(const ServerInfo& info)
{
    {
        std::unique_lock<std::shared_mutex> lock(m_serversMutex);
        m_servers[info.serverId] = info;
    }

    LOG_WRITE(LogLevel::Info, "RegistryClient: server info updated. serverId=" + std::to_string(info.serverId)
             + " type=" + std::to_string(static_cast<int>(info.serverType))
             + " status=" + std::to_string(static_cast<int>(info.status)));

    if (m_serverInfoCallback)
        m_serverInfoCallback(info);
}

std::vector<ServerInfo> RegistryClient::GetServerList(ServerType type) const
{
    std::vector<ServerInfo> result;
    std::shared_lock<std::shared_mutex> lock(m_serversMutex);
    for (const auto& [id, info] : m_servers)
    {
        if (info.serverType == type)
            result.push_back(info);
    }
    return result;
}

} // namespace serverbase
