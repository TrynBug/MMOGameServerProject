#include "pch.h"

#include "GameDataLib.h"

#include "ServerBase.h"

#include "IoContext.h"
#include "NetServer.h"
#include "NetClient.h"

namespace serverbase
{

bool ServerBase::Initialize(const ServerBaseConfig& config)
{
    m_config = config;

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
    LOG_WRITE(LogLevel::Info, std::format("{} starting... serverId={}", serverTypeName, m_serverId));

    // 서버ID 세팅
    if (config.serverId <= 0 || config.serverId > 999)
    {
        LOG_WRITE(LogLevel::Error, std::format("invalid serverId in config : {} (must be 1~999)", config.serverId));
        return false;
    }

    m_serverId = config.serverId;

    // ObjectIdGenerator 초기화
    m_objectIdGenerator.Initialize(m_serverId);

    // 현재경로 얻기
    const std::filesystem::path currentDir = std::filesystem::current_path();

    // 게임데이터 초기화
    const std::filesystem::path gameDataDir = currentDir.parent_path() / "GameData";
    if (false == GameDataManager::LoadAllGameData(gameDataDir.string()))
    {
        LOG_WRITE(LogLevel::Error, std::format("GameDataManager::LoadAllGameData failed"));
        return false;
    }

    // IoContext (IOCP + worker 스레드) 초기화
    if (!m_ioContext.Initialize(config.ioContextConfig))
    {
        LOG_WRITE(LogLevel::Error, "ServerBase::Initialize - IoContext Initialize failed");
        return false;
    }

    LOG_WRITE(LogLevel::Info, "ServerBase::Initialize - IoContext initialized");

	// Listen용 NetServer 초기화 (다른 서버/클라이언트의 접속을 받는 서버만)
    if (config.useListenServer)
    {
        netlib::FuncEventHandler* pHandler = GetListenEventHandler();
        if (!pHandler)
        {
            LOG_WRITE(LogLevel::Error, "ServerBase::Initialize - useListenServer=true but GetListenEventHandler() returned nullptr");
            return false;
        }

        m_spListenServer = std::make_unique<netlib::NetServer>(&m_ioContext);
        if (!m_spListenServer->Initialize(config.listenServerConfig))
        {
            LOG_WRITE(LogLevel::Error, "ServerBase::Initialize - listen NetServer Initialize failed");
            return false;
        }

        m_spListenServer->SetEventHandler(pHandler);

        LOG_WRITE(LogLevel::Info, std::format("ServerBase::Initialize - listen NetServer initialized on port {}", config.listenServerConfig.port));
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

        LOG_WRITE(LogLevel::Info, std::format("ServerBase::Initialize - {} contents thread(s) started", config.numContentsThreads));
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
            LOG_WRITE(LogLevel::Error, "ServerBase::Initialize - RegistryClient Initialize failed");
            return false;
        }

        // 등록 거부 시 서버 종료
        m_spRegistryClient->SetRegisterRejectedCallback([this](const std::string& reason)
        {
            LOG_WRITE(LogLevel::Error, std::format("ServerBase: register rejected by registry - {}. shutting down.", reason));
            RequestShutdown();
        });

        m_spRegistryClient->SetServerInfoCallback([this](const ServerInfo& info)
        {
            OnServerInfoUpdated(info);
        });

        LOG_WRITE(LogLevel::Info, "ServerBase::Initialize - RegistryClient initialized");
    }

    // 서브클래스 hook 함수 호출
    if (!OnInitialize())
    {
        LOG_WRITE(LogLevel::Error, "ServerBase::Initialize - OnInitialize() failed");
        return false;
    }

    m_bRunning = true;
    LOG_WRITE(LogLevel::Info, std::format("ServerBase::Initialize - {} initialized successfully", serverTypeName));
    return true;
}

bool ServerBase::StartAccept()
{
    if (!m_spListenServer)
    {
        LOG_WRITE(LogLevel::Warn, "ServerBase::StartAccept - no listen NetServer configured");
        return false;
    }

    if (!m_spListenServer->StartAccept())
    {
        LOG_WRITE(LogLevel::Error, "ServerBase::StartAccept - StartAccept failed");
        return false;
    }

    LOG_WRITE(LogLevel::Info, std::format("ServerBase::StartAccept - accepting connections on port {}", m_config.listenServerConfig.port));

    return true;
}

bool ServerBase::StartRegistryClient()
{
    if (!m_spRegistryClient)
    {
        LOG_WRITE(LogLevel::Warn, "ServerBase::StartRegistryClient - no RegistryClient configured");
        return false;
    }

    m_spRegistryClient->Start();
    LOG_WRITE(LogLevel::Info, "ServerBase::StartRegistryClient - started");
    return true;
}

void ServerBase::Run()
{
    LOG_WRITE(LogLevel::Info, "ServerBase::Run - server is running. waiting for shutdown signal...");

    std::unique_lock<std::mutex> lock(m_shutdownMutex);
    m_shutdownCv.wait(lock, [this] { return !m_bRunning.load(); });

    LOG_WRITE(LogLevel::Info, "ServerBase::Run - shutdown signal received. shutting down...");
    shutdownInternal();
}

void ServerBase::RequestShutdown()
{
    if (m_bShuttingDown.exchange(true))
        return;  // 이미 종료 진행 중

    LOG_WRITE(LogLevel::Info, "ServerBase::RequestShutdown - graceful shutdown requested");

    // 레지스트리에 종료 알림
    if (m_spRegistryClient && m_spRegistryClient->IsRegistered())
        m_spRegistryClient->SendShutdownNotify();

    // Listen 서버 Accept 중단 (신규 접속 차단)
    if (m_spListenServer)
    {
        m_spListenServer->StopAccept();
        LOG_WRITE(LogLevel::Info, "ServerBase::RequestShutdown - listen accept stopped");
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
    LOG_WRITE(LogLevel::Info, "ServerBase::shutdownInternal - begin");

    // 컨텐츠 스레드 정지
    for (auto& spThread : m_contentsThreads)
        spThread->Stop();

    m_contentsThreads.clear();
    LOG_WRITE(LogLevel::Info, "ServerBase::shutdownInternal - contents threads stopped");

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
    LOG_WRITE(LogLevel::Info, "ServerBase::shutdownInternal - server connections closed");

    // Listen NetServer 종료
    if (m_spListenServer)
    {
        m_spListenServer->Shutdown();
        m_spListenServer.reset();
        LOG_WRITE(LogLevel::Info, "ServerBase::shutdownInternal - listen NetServer shutdown");
    }

    // IoContext 종료 (Worker 스레드 종료)
    m_ioContext.Shutdown();
    LOG_WRITE(LogLevel::Info, "ServerBase::shutdownInternal - IoContext shutdown");

    // 서브클래스 훅
    OnShutdown();

    LOG_WRITE(LogLevel::Info, "ServerBase::shutdownInternal - complete");
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
        LOG_WRITE(LogLevel::Error, std::format("ServerBase::ConnectToServer - Initialize failed for {}:{}", ip, port));
        return nullptr;
    }

    spClient->SetEventHandler(&handler);
    spClient->Connect(ip, port);  // 연결시도 시작. 연결 안되면 자동으로 재연결 시도함

    {
        std::lock_guard<std::mutex> lock(m_serverConnectionsMutex);
        m_serverConnections.push_back(spClient);
    }

    LOG_WRITE(LogLevel::Info, std::format("ServerBase::ConnectToServer - connecting to {}:{}", ip, port));
    return spClient;
}

void ServerBase::DisconnectToServer(netlib::NetClientPtr spClient)
{
    if(!spClient)
		return;

    std::lock_guard<std::mutex> lock(m_serverConnectionsMutex);
    auto iter = std::find_if(m_serverConnections.begin(), m_serverConnections.end(),
        [&spClient](const netlib::NetClientPtr& sp) { return sp.get() == spClient.get(); });

    if (iter != m_serverConnections.end())
    {
        (*iter)->Shutdown();
        m_serverConnections.erase(iter);
    }
}

void ServerBase::AssignContents(int32 threadIndex, ContentsPtr spContents)
{
    if (threadIndex < 0 || threadIndex >= static_cast<int32>(m_contentsThreads.size()))
    {
        LOG_WRITE(LogLevel::Error, std::format("ServerBase::AssignContents - invalid threadIndex: {}", threadIndex));
        return;
    }
    m_contentsThreads[threadIndex]->AddContents(std::move(spContents));
}

void ServerBase::RemoveContents(int32 threadIndex, ContentsPtr spContents)
{
    if (threadIndex < 0 || threadIndex >= static_cast<int32>(m_contentsThreads.size()))
    {
        LOG_WRITE(LogLevel::Error, std::format("ServerBase::RemoveContents - invalid threadIndex: {}", threadIndex));
        return;
    }
    m_contentsThreads[threadIndex]->RemoveContents(std::move(spContents));
}

} // namespace serverbase
