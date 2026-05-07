#include "pch.h"
#include "ServerBase.h"

#include "IoContext.h"
#include "NetServer.h"
#include "NetClient.h"

namespace serverbase
{

bool ServerBase::Initialize(const ServerBaseConfig& config)
{
    m_config = config;

    // 서버ID 검증 및 즉시 세팅 (Config에서 로드한 값을 부팅 즉시 사용)
    if (config.serverId <= 0 || config.serverId > 999)
    {
        // Logger가 아직 초기화되지 않았을 수 있으므로 stderr에도 출력
        std::cerr << "ServerBase::Initialize - invalid serverId in config: "
                  << config.serverId << " (must be 1~999)" << std::endl;
        return false;
    }

    m_serverId = config.serverId;

	// ObjectIdGenerator 초기화 (serverId 세팅)
    m_objectIdGenerator.Initialize(m_serverId);

    // Logger 초기화
    std::string serverTypeName;
    switch (config.serverType)
    {
    case ServerType::Registry: serverTypeName = "RegistryServer"; break;
    case ServerType::Login:    serverTypeName = "LoginServer";    break;
    case ServerType::Gateway:  serverTypeName = "GatewayServer";  break;
    case ServerType::Game:     serverTypeName = "GameServer";     break;
    case ServerType::Chat:     serverTypeName = "ChatServer";     break;
    default:                   serverTypeName = "Server";         break;
    }

    Logger::Initialize(config.logDir, serverTypeName, config.logLevel);
    LOG_INFO(serverTypeName + " starting... serverId=" + std::to_string(m_serverId));

    // IoContext (IOCP + worker 스레드) 초기화
    if (!m_ioContext.Initialize(config.ioContextConfig))
    {
        LOG_ERROR("ServerBase::Initialize - IoContext Initialize failed");
        return false;
    }

    LOG_INFO("ServerBase::Initialize - IoContext initialized");

	// Listen용 NetServer 초기화 (다른 서버/클라이언트의 접속을 받는 서버만)
    if (config.useListenServer)
    {
        netlib::FuncEventHandler* pHandler = GetListenEventHandler();
        if (!pHandler)
        {
            LOG_ERROR("ServerBase::Initialize - useListenServer=true but GetListenEventHandler() returned nullptr");
            return false;
        }

        m_spListenServer = std::make_unique<netlib::NetServer>(&m_ioContext);
        if (!m_spListenServer->Initialize(config.listenServerConfig))
        {
            LOG_ERROR("ServerBase::Initialize - listen NetServer Initialize failed");
            return false;
        }

        m_spListenServer->SetEventHandler(pHandler);

        LOG_INFO("ServerBase::Initialize - listen NetServer initialized on port " + std::to_string(config.listenServerConfig.port));
    }

    // 컨텐츠 스레드 생성
    if (config.numContentsThreads > 0)
    {
        for (int32 i = 0; i < config.numContentsThreads; ++i)
        {
            auto spThread = std::make_shared<ContentsThread>(config.contentsUpdateMs);
            spThread->Start();
            m_contentsThreads.push_back(std::move(spThread));
        }

        LOG_INFO("ServerBase::Initialize - " + std::to_string(config.numContentsThreads) + " contents thread(s) started");
    }

    // 타이머 시작
    m_timer.Start();

    // Registry 서버와 연결하는 RegistryClient 초기화 (레지스트리 서버 자신은 사용안함)
    if (config.useRegistry)
    {
        RegistryClient::Config regConfig;
        regConfig.registryIp        = config.registryIp;
        regConfig.registryPort      = config.registryPort;
        regConfig.myServerType      = config.serverType;
        regConfig.myServerId        = m_serverId;            // config에서 로드한 ID 전달
        regConfig.myIp              = config.serverIp;
        regConfig.myPort            = config.serverPort;
        regConfig.pollTargetTypes   = config.pollTargetTypes;
        regConfig.userCountReportMs = config.userCountReportMs;

        m_spRegistryClient = std::make_unique<RegistryClient>();
        if (!m_spRegistryClient->Initialize(this, regConfig))
        {
            LOG_ERROR("ServerBase::Initialize - RegistryClient Initialize failed");
            return false;
        }

        // 등록 거부 시 서버 종료
        m_spRegistryClient->SetRegisterRejectedCallback([this](const std::string& reason)
        {
            LOG_ERROR("ServerBase: register rejected by registry - " + reason + ". shutting down.");
            RequestShutdown();
        });

        m_spRegistryClient->SetServerInfoCallback([this](const ServerInfo& info)
        {
            OnServerInfoUpdated(info);
        });

        LOG_INFO("ServerBase::Initialize - RegistryClient initialized");
    }

    // 서브클래스 hook 함수 호출
    if (!OnInitialize())
    {
        LOG_ERROR("ServerBase::Initialize - OnInitialize() failed");
        return false;
    }

    m_bRunning = true;
    LOG_INFO("ServerBase::Initialize - " + serverTypeName + " initialized successfully");
    return true;
}

bool ServerBase::StartAccept()
{
    if (!m_spListenServer)
    {
        LOG_WARN("ServerBase::StartAccept - no listen NetServer configured");
        return false;
    }

    if (!m_spListenServer->StartAccept())
    {
        LOG_ERROR("ServerBase::StartAccept - StartAccept failed");
        return false;
    }

    LOG_INFO("ServerBase::StartAccept - accepting connections on port " + std::to_string(m_config.listenServerConfig.port));

    return true;
}

bool ServerBase::StartRegistryClient()
{
    if (!m_spRegistryClient)
    {
        LOG_WARN("ServerBase::StartRegistryClient - no RegistryClient configured");
        return false;
    }

    m_spRegistryClient->Start();
    LOG_INFO("ServerBase::StartRegistryClient - started");
    return true;
}

void ServerBase::Run()
{
    LOG_INFO("ServerBase::Run - server is running. waiting for shutdown signal...");

    std::unique_lock<std::mutex> lock(m_shutdownMutex);
    m_shutdownCv.wait(lock, [this] { return !m_bRunning.load(); });

    LOG_INFO("ServerBase::Run - shutdown signal received. shutting down...");
    shutdownInternal();
}

void ServerBase::RequestShutdown()
{
    if (m_bShuttingDown.exchange(true))
        return;  // 이미 종료 진행 중

    LOG_INFO("ServerBase::RequestShutdown - graceful shutdown requested");

    // 레지스트리에 종료 알림
    if (m_spRegistryClient && m_spRegistryClient->IsRegistered())
        m_spRegistryClient->SendShutdownNotify();

    // Listen 서버 Accept 중단 (신규 접속 차단)
    if (m_spListenServer)
    {
        m_spListenServer->StopAccept();
        LOG_INFO("ServerBase::RequestShutdown - listen accept stopped");
    }

    // 서브클래스 훅 (유저 이탈 대기 등)
    OnBeforeShutdown();

    // Run() 블로킹 해제
    {
        std::lock_guard<std::mutex> lock(m_shutdownMutex);
        m_bRunning = false;
    }
    m_shutdownCv.notify_all();
}

void ServerBase::shutdownInternal()
{
    LOG_INFO("ServerBase::shutdownInternal - begin");

    // 컨텐츠 스레드 정지
    for (auto& spThread : m_contentsThreads)
        spThread->Stop();

    m_contentsThreads.clear();
    LOG_INFO("ServerBase::shutdownInternal - contents threads stopped");

    // 타이머 정지
    m_timer.Stop();

    // RegistryClient 정지
    if (m_spRegistryClient)
    {
        m_spRegistryClient->Stop();
        m_spRegistryClient.reset();
    }

    // 서버간 연결 종료
    {
        std::lock_guard<std::mutex> lock(m_serverConnectionsMutex);
        for (auto& spClient : m_serverConnections)
            spClient->Shutdown();

        m_serverConnections.clear();
    }
    LOG_INFO("ServerBase::shutdownInternal - server connections closed");

    // Listen NetServer 종료
    if (m_spListenServer)
    {
        m_spListenServer->Shutdown();
        m_spListenServer.reset();
        LOG_INFO("ServerBase::shutdownInternal - listen NetServer shutdown");
    }

    // IoContext 종료 (Worker 스레드 종료)
    m_ioContext.Shutdown();
    LOG_INFO("ServerBase::shutdownInternal - IoContext shutdown");

    // 서브클래스 훅
    OnShutdown();

    LOG_INFO("ServerBase::shutdownInternal - complete");
    Logger::Shutdown();
}

netlib::NetClientPtr ServerBase::ConnectToServer(const std::string& ip, uint16 port, netlib::FuncEventHandler& handler)
{
    netlib::NetClientConfig clientConfig;
    clientConfig.bUseNagle = false;
    clientConfig.bAutoReconnect = true;
    clientConfig.reconnectIntervalMs  = 60000;  // 재연결 시도 대기시간

    auto spClient = std::make_shared<netlib::NetClient>(&m_ioContext);
    if (!spClient->Initialize(clientConfig))
    {
        LOG_ERROR("ServerBase::ConnectToServer - Initialize failed for " + ip + ":" + std::to_string(port));
        return nullptr;
    }

    spClient->SetEventHandler(&handler);
    spClient->Connect(ip, port);  // 연결시도 시작. 연결 안되면 자동으로 재연결 시도함

    {
        std::lock_guard<std::mutex> lock(m_serverConnectionsMutex);
        m_serverConnections.push_back(spClient);
    }

    LOG_INFO("ServerBase::ConnectToServer - connecting to " + ip + ":" + std::to_string(port));
    return spClient;
}

void ServerBase::DisconnectToServer(netlib::NetClientPtr spClient)
{
    if(!spClient)
		return;

    std::lock_guard<std::mutex> lock(m_serverConnectionsMutex);
    auto it = std::find_if(m_serverConnections.begin(), m_serverConnections.end(),
        [&spClient](const netlib::NetClientPtr& sp) { return sp.get() == spClient.get(); });

    if (it != m_serverConnections.end())
    {
        (*it)->Shutdown();
        m_serverConnections.erase(it);
    }
}

void ServerBase::AssignContents(int32 threadIndex, ContentsPtr spContents)
{
    if (threadIndex < 0 || threadIndex >= static_cast<int32>(m_contentsThreads.size()))
    {
        LOG_ERROR("ServerBase::AssignContents - invalid threadIndex: " + std::to_string(threadIndex));
        return;
    }
    m_contentsThreads[threadIndex]->AddContents(std::move(spContents));
}

void ServerBase::RemoveContents(int32 threadIndex, ContentsPtr spContents)
{
    if (threadIndex < 0 || threadIndex >= static_cast<int32>(m_contentsThreads.size()))
    {
        LOG_ERROR("ServerBase::RemoveContents - invalid threadIndex: " + std::to_string(threadIndex));
        return;
    }
    m_contentsThreads[threadIndex]->RemoveContents(std::move(spContents));
}

} // namespace serverbase
