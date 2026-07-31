#include "pch.h"

#include "GameDataLib.h"

#include "ServerBase.h"

#include "IoContext.h"
#include "NetServer.h"
#include "NetClient.h"

namespace serverbase
{

namespace
{
    // 구체적인 IPv4 리터럴인지 검사한다. 빈값 / "0.0.0.0" / 도메인명 / 형식오류는 모두 false.
    bool IsConcreteIPv4(const std::string& ip)
    {
        if (ip.empty())
            return false;

        in_addr addr{};
        if (::inet_pton(AF_INET, ip.c_str(), &addr) != 1)
            return false;

        if (addr.s_addr == INADDR_ANY)   // 0.0.0.0
            return false;

        return true;
    }

    const char* GetMetricServerTypeName(ServerType serverType)
    {
        switch (serverType)
        {
        case ServerType::Registry:      return "registry";
        case ServerType::Login:         return "login";
        case ServerType::Gateway:       return "gateway";
        case ServerType::Game:          return "game";
        case ServerType::Communication: return "communication";
        default:                        return "unknown";
        }
    }
}

bool ServerBase::Initialize(const ServerBaseConfig& config)
{
    m_config = config;
    m_serverStartTime = std::chrono::steady_clock::now();

    // Logger 초기화
    std::string serverTypeName;
    switch (config.serverType)
    {
    case ServerType::Registry: serverTypeName = "RegistryServer"; break;
    case ServerType::Login:    serverTypeName = "LoginServer";    break;
    case ServerType::Gateway:  serverTypeName = "GatewayServer";  break;
    case ServerType::Game:     serverTypeName = "GameServer";     break;
    case ServerType::Communication: serverTypeName = "CommunicationServer"; break;
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

    // 네트워크 IP 설정 검증
    if (!IsConcreteIPv4(config.privateIp))
    {
        LOG_WRITE(LogLevel::Error, std::format("invalid PrivateIP in config : '{}' (구체적인 IPv4 주소여야 함. 빈값/0.0.0.0/도메인명 불가)", config.privateIp));
        return false;
    }

    if (config.serverType == ServerType::Gateway)
    {
        // 게이트웨이는 클라이언트에게 접속주소(PublicIP)를 주어야 하므로 필수
        if (!IsConcreteIPv4(config.publicIp))
        {
            LOG_WRITE(LogLevel::Error, std::format("invalid PublicIP in config : '{}' (게이트웨이는 클라이언트 광고용 PublicIP가 필수)", config.publicIp));
            return false;
        }
    }
    else
    {
        // 그 외 서버는 PublicIP 없어도됨. 값이 지정된 경우에만 형식을 검증한다.
        if (!config.publicIp.empty() && !IsConcreteIPv4(config.publicIp))
        {
            LOG_WRITE(LogLevel::Error, std::format("invalid PublicIP in config : '{}'", config.publicIp));
            return false;
        }
    }

    if (config.metricsEnabled)
    {
        if (!IsConcreteIPv4(config.metricsIp) || config.metricsPort == 0)
        {
            LOG_WRITE(LogLevel::Error, std::format("invalid Monitoring config. IP='{}' Port={}", config.metricsIp, config.metricsPort));
            return false;
        }
    }

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
        LOG_WRITE(LogLevel::Error, "IoContext Initialize failed");
        return false;
    }

    LOG_WRITE(LogLevel::Info, "IoContext initialized");

    if (config.metricsEnabled)
    {
        // DB metric sink는 DB queue가 요청을 받기 전에 연결한다. 이후 enqueue/worker callback이 Counter와 Gauge를 직접 갱신한다.
        if (!m_dbMetricsPublisher.Initialize(m_metricsRegistry, m_dbQueue))
        {
            LOG_WRITE(LogLevel::Error, "failed to register DB metrics");
            return false;
        }
    }

	// 클라이언트 Listen용 NetServer 초기화
    if (config.useClientListenServer)
    {
        netlib::FuncEventHandler* pHandler = GetClientListenEventHandler();
        if (!pHandler)
        {
            LOG_WRITE(LogLevel::Error, "useClientListenServer=true but GetClientListenEventHandler() returned nullptr");
            return false;
        }

        m_spClientListenServer = std::make_unique<netlib::NetServer>(&m_ioContext);
        if (!m_spClientListenServer->Initialize(config.clientListenServerConfig))
        {
            LOG_WRITE(LogLevel::Error, "client listen NetServer Initialize failed");
            return false;
        }

        m_spClientListenServer->SetEventHandler(pHandler);
        LOG_WRITE(LogLevel::Info, std::format("client listen NetServer initialized on port {}", config.clientListenServerConfig.port));
    }

    // 내부 서버 Listen용 NetServer 초기화
    if (config.useInternalListenServer)
    {
        netlib::FuncEventHandler* pHandler = GetInternalListenEventHandler();
        if (!pHandler)
        {
            LOG_WRITE(LogLevel::Error, "useInternalListenServer=true but GetInternalListenEventHandler() returned nullptr");
            return false;
        }

        m_spInternalListenServer = std::make_unique<netlib::NetServer>(&m_ioContext);
        if (!m_spInternalListenServer->Initialize(config.internalListenServerConfig))
        {
            LOG_WRITE(LogLevel::Error, "internal listen NetServer Initialize failed");
            return false;
        }
        
        m_spInternalListenServer->SetEventHandler(pHandler);
        LOG_WRITE(LogLevel::Info, std::format("internal listen NetServer initialized on port {}", config.internalListenServerConfig.port));
    }

    // 컨텐츠 스레드 생성
    if (config.numContentsThreads > 0)
    {
        for (int32 i = 0; i < config.numContentsThreads; ++i)
        {
            auto spThread = std::make_shared<ContentsThread>(config.contentsUpdateMs);
            if (config.metricsEnabled && !spThread->InitializeMetrics(m_metricsRegistry, i == 0))
            {
                LOG_WRITE(LogLevel::Error, std::format("failed to register ContentsThread metrics. thread={}", i));
                return false;
            }
            spThread->Start();
            m_contentsThreads.push_back(std::move(spThread));
        }

        LOG_WRITE(LogLevel::Info, std::format("{} contents thread(s) started", config.numContentsThreads));
    }

    // 타이머 시작
    m_timer.Start();

    // Registry 서버와 연결하는 RegistryClient 초기화 (레지스트리 서버 자신은 사용안함)
    if (config.useRegistry)
    {
        RegistryClient::RegistryClientConfig regConfig;
        regConfig.registryIp        = config.registryIp;
        regConfig.registryPort      = config.registryPort;
        regConfig.myServerType      = config.serverType;
        regConfig.myServerId        = m_serverId;            // config에서 로드한 ID 전달
        regConfig.myPrivateIp       = config.privateIp;
        regConfig.myPublicIp        = config.publicIp;
        regConfig.myClientPort      = config.clientListenServerConfig.port;
        regConfig.myInternalPort    = config.internalListenServerConfig.port;
        regConfig.pollTargetTypes   = config.pollTargetTypes;
        regConfig.pollIntervalMs    = config.registryPollIntervalMs;
        regConfig.userCountReportMs = config.userCountReportMs;

        m_spRegistryClient = std::make_unique<RegistryClient>();
        if (!m_spRegistryClient->Initialize(this, regConfig))
        {
            LOG_WRITE(LogLevel::Error, "RegistryClient Initialize failed");
            return false;
        }

        // 등록 거부 시 서버 종료
        m_spRegistryClient->SetRegisterRejectedCallback([this](const std::string& reason)
        {
            LOG_WRITE(LogLevel::Error, std::format("register rejected by registry - {}. shutting down.", reason));

            RequestShutdown();
        });

        m_spRegistryClient->SetServerInfoCallback([this](const ServerInfo& info)
        {
            OnServerInfoUpdated(info);
        });

        LOG_WRITE(LogLevel::Info, "RegistryClient initialized");
    }

    // DB 초기화 (ServerBaseConfig의 [AccountDB]/[GameDB] 설정에 따라 자동). OnInitialize 전에 열어 서브클래스가 쓸 수 있게 한다.
    if (!initializeDatabases())
    {
        LOG_WRITE(LogLevel::Error, "initializeDatabases() failed");
        return false;
    }

    // 서브클래스 hook 함수 호출
    if (!OnInitialize())
    {
        LOG_WRITE(LogLevel::Error, "OnInitialize() failed");
        return false;
    }

    if (config.metricsEnabled)
    {
        // server_info는 값 자체보다 server_type/server_id label을 제공하는 정보성 metric이다.
        // ready와 shutting_down을 분리해 "프로세스는 살아 있지만 아직 준비 안 됨"과 graceful shutdown 상태를 구분한다.
        bool lifecycleMetricsRegistered = true;
        lifecycleMetricsRegistered &= m_metricsRegistry.AddGauge(GaugeMetric::ServerBase_Info,
            "mmo_server_info", "Static information about this server process.",
            { { "server_type", GetMetricServerTypeName(config.serverType) }, { "server_id", std::to_string(m_serverId) } });
        lifecycleMetricsRegistered &= m_metricsRegistry.AddGauge(GaugeMetric::ServerBase_Ready,
            "mmo_server_ready", "Whether the server completed initialization and can process work.");
        lifecycleMetricsRegistered &= m_metricsRegistry.AddGauge(GaugeMetric::ServerBase_ShuttingDown,
            "mmo_server_shutting_down", "Whether graceful shutdown has started.");
        lifecycleMetricsRegistered &= m_metricsRegistry.AddGauge(GaugeMetric::ServerBase_UptimeSeconds,
            "mmo_server_uptime_seconds", "Seconds elapsed since server initialization started.");
        if (!lifecycleMetricsRegistered)
        {
            LOG_WRITE(LogLevel::Error, "failed to register common server metrics");
            return false;
        }

        m_metricsRegistry.Set(GaugeMetric::ServerBase_Info, 1.0);
        m_metricsRegistry.Set(GaugeMetric::ServerBase_Ready, 0.0);
        m_metricsRegistry.Set(GaugeMetric::ServerBase_ShuttingDown, 0.0);

        // IoContext/PacketPool 초기화가 끝난 뒤 등록해야 실제 capacity bucket 목록을 고정 label로 만들 수 있다.
        if (!m_netMetricsPublisher.Initialize(m_metricsRegistry, m_ioContext))
        {
            LOG_WRITE(LogLevel::Error, "failed to register NetLib metrics");
            return false;
        }

        // process metric은 모든 서버가 게시한다. 같은 Windows host 값이 서버 수만큼 중복되지 않도록 host metric은 Registry만 게시한다.
        if (!m_windowsMetricsPublisher.Initialize(m_metricsRegistry, config.serverType == ServerType::Registry))
        {
            LOG_WRITE(LogLevel::Error, "failed to register Windows process/host metrics");
            return false;
        }

        // 모든 publisher와 서버별 metric 등록이 끝난 뒤 endpoint를 연다.
        // 이 순서로 시작해야 첫 Prometheus scrape부터 완전한 family 목록을 반환한다.
        MetricsHttpServerConfig metricsConfig;
        metricsConfig.ip = config.metricsIp;
        metricsConfig.port = config.metricsPort;

        std::string metricsError;
        if (!m_metricsHttpServer.Start(metricsConfig, m_metricsRegistry, [this]()
            {
                // 이 callback은 /metrics를 처리하는 monitoring worker에서 실행된다.
                // publisher는 각 subsystem의 atomic/저비용 snapshot만 Registry에 복사하고, 마지막에 서버별 snapshot hook을 호출한다.
                const double uptimeSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_serverStartTime).count();
                m_metricsRegistry.Set(GaugeMetric::ServerBase_UptimeSeconds, uptimeSeconds);
                m_netMetricsPublisher.Publish();
                m_dbMetricsPublisher.Publish();
                m_windowsMetricsPublisher.Publish();
                publishContentsThreadMetrics();
                OnMetricsCollect();
            }, metricsError))
        {
            LOG_WRITE(LogLevel::Error, std::format("metrics HTTP server start failed. {}", metricsError));
            return false;
        }

        LOG_WRITE(LogLevel::Info, std::format("metrics HTTP server listening on {}:{}", config.metricsIp, config.metricsPort));
    }

    m_bRunning = true;
    // HTTP endpoint가 성공적으로 열린 뒤에만 ready=1로 바꾼다. Prometheus가 초기화 도중 target을 정상 서비스로 오인하지 않게 한다.
    m_metricsRegistry.Set(GaugeMetric::ServerBase_Ready, 1.0);
    LOG_WRITE(LogLevel::Info, std::format("{} initialized successfully", serverTypeName));
    return true;
}

void ServerBase::publishContentsThreadMetrics()
{
    int64 contentsCount = 0;
    int64 pendingAdd = 0;
    int64 pendingRemove = 0;
    int64 taskQueueDepth = 0;
    double taskOldestAgeSeconds = 0.0;
    for (const ContentsThreadPtr& spThread : m_contentsThreads)
    {
        const ContentsThread::MetricsSnapshot snapshot = spThread->GetMetricsSnapshot();
        contentsCount += snapshot.contentsCount;
        pendingAdd += snapshot.pendingAdd;
        pendingRemove += snapshot.pendingRemove;
        taskQueueDepth += snapshot.taskQueueDepth;
        taskOldestAgeSeconds = std::max(taskOldestAgeSeconds, snapshot.taskOldestAgeSeconds);
    }

    m_metricsRegistry.Set(GaugeMetric::ContentsThreads_ContentsTotal, static_cast<double>(contentsCount));
    m_metricsRegistry.Set(GaugeMetric::ContentsThreads_PendingAddTotal, static_cast<double>(pendingAdd));
    m_metricsRegistry.Set(GaugeMetric::ContentsThreads_PendingRemoveTotal, static_cast<double>(pendingRemove));
    m_metricsRegistry.Set(GaugeMetric::ContentsThreads_TaskQueueDepthTotal, static_cast<double>(taskQueueDepth));
    m_metricsRegistry.Set(GaugeMetric::ContentsThreads_TaskOldestAgeSecondsMax, taskOldestAgeSeconds);
}

// ServerBaseConfig의 DB 설정에 따라 AccountDB/GameDB 샤드를 m_dbQueue에 연다.
bool ServerBase::initializeDatabases()
{
    if (!m_config.useAccountDB && !m_config.useGameDB)
        return true;   // DB를 쓰지 않는 서버

    // GameDB는 AccountDB(샤드 목록을 담은 GameDBRegistry 보관)가 반드시 있어야 한다.
    if (m_config.useGameDB && !m_config.useAccountDB)
    {
        LOG_WRITE(LogLevel::Error, "GameDB requires AccountDB. Set [AccountDB] in ini.");
        return false;
    }

    std::vector<db::AsyncDBQueue::OpenEntry> entries;

    if (m_config.useAccountDB)
    {
        const auto& cfg = m_config.accountDBConfig;
        if (cfg.host.empty() || cfg.user.empty() || cfg.database.empty())
        {
            LOG_WRITE(LogLevel::Error, "AccountDB config incomplete. Need [AccountDB] Host/User/DBName.");
            return false;
        }
        entries.push_back({ db::EDBType::Account, 0, cfg });
    }

    if (m_config.useGameDB)
    {
        if (!loadGameShards(entries))
            return false;
    }

    if (!m_dbQueue.Open(entries, m_config.dbNumWorkers))
    {
        LOG_WRITE(LogLevel::Error, std::format("DB pool open failed. {}", m_dbQueue.GetLastError()));
        return false;
    }

    LOG_WRITE(LogLevel::Info, std::format("DB pool opened. databases={} (account={} game={}) workers={}",
        entries.size(), m_config.useAccountDB ? 1 : 0, entries.size() - (m_config.useAccountDB ? 1 : 0),
        m_config.dbNumWorkers));
    return true;
}

// AccountDB의 GameDBRegistry(status='active')를 읽어 GameDB 샤드 등록정보를 entries에 추가한다.
// 동기 DBConnection으로 1회 수행. host/port/db는 레지스트리, user/password는 AccountDB와 공유.
bool ServerBase::loadGameShards(std::vector<db::AsyncDBQueue::OpenEntry>& entries)
{
    db::DBConnection conn;
    if (!conn.Open(m_config.accountDBConfig))
    {
        LOG_WRITE(LogLevel::Error, std::format("loadGameShards - AccountDB connect failed. {}", conn.GetLastError()));
        return false;
    }

    db::DBResult result = conn.Execute(
        "SELECT game_db_index, host, port, db_name FROM GameDBRegistry WHERE status = 'active'");
    if (!result.success)
    {
        LOG_WRITE(LogLevel::Error, std::format("loadGameShards - registry query failed. {}", result.errorMsg));
        return false;
    }
    if (result.IsEmpty())
    {
        LOG_WRITE(LogLevel::Error, "loadGameShards - no active GameDB shard in GameDBRegistry.");
        return false;
    }

    for (int row = 0; row < result.RowCount(); ++row)
    {
        const int index = static_cast<int>(result.GetInt64(row, "game_db_index", -1));

        db::DBConnectionConfig shardCfg;
        shardCfg.host     = result.GetString(row, "host");
        shardCfg.port     = static_cast<unsigned int>(result.GetInt64(row, "port", 3306));
        shardCfg.user     = m_config.accountDBConfig.user;       // 샤드는 AccountDB와 자격증명 공유
        shardCfg.password = m_config.accountDBConfig.password;
        shardCfg.database = result.GetString(row, "db_name");

        if (index < 0 || shardCfg.host.empty() || shardCfg.database.empty())
        {
            LOG_WRITE(LogLevel::Error, std::format("loadGameShards - invalid registry row. index={} host={} db={}",
                index, shardCfg.host, shardCfg.database));
            return false;
        }
        entries.push_back({ db::EDBType::Game, index, shardCfg });
    }

    LOG_WRITE(LogLevel::Info, std::format("GameDB shards from registry: {}", result.RowCount()));
    return true;
}

// ini의 [AccountDB]/[GameDB] 섹션을 읽어 ServerBaseConfig의 DB 필드를 채운다.
void LoadDBConfigFromIni(ServerBaseConfig& config, ConfigParser& parser)
{
    // [AccountDB] : Host가 있으면 사용
    const std::string accHost = parser.GetString("AccountDB", "Host", "");
    if (!accHost.empty())
    {
        config.useAccountDB = true;
        config.accountDBConfig.host     = accHost;
        config.accountDBConfig.port     = static_cast<unsigned int>(parser.GetInt32("AccountDB", "Port", 3306));
        config.accountDBConfig.user     = parser.GetString("AccountDB", "User", "");
        config.accountDBConfig.password = parser.GetString("AccountDB", "Password", "");
        config.accountDBConfig.database = parser.GetString("AccountDB", "DBName", "");
    }

    // [GameDB] : Enabled=true 면 사용 (샤드 목록은 GameDBRegistry, 자격증명은 AccountDB와 공유)
    config.useGameDB = parser.GetBool("GameDB", "Enabled", false);
}

void LoadMetricsConfigFromIni(ServerBaseConfig& config, ConfigParser& parser)
{
    config.metricsEnabled = parser.GetBool("Monitoring", "Enabled", false);
    config.metricsIp = parser.GetString("Monitoring", "IP", "127.0.0.1");
    const int32 metricsPort = parser.GetInt32("Monitoring", "Port", 0);
    config.metricsPort = (metricsPort > 0 && metricsPort <= 65535) ? static_cast<uint16>(metricsPort) : 0;
}

bool ServerBase::StartAccept()
{
    bool started = false;

    if (m_spClientListenServer)
    {
        if (!m_spClientListenServer->StartAccept())
        {
            LOG_WRITE(LogLevel::Error, "client listen StartAccept failed");
            return false;
        }
        LOG_WRITE(LogLevel::Info, std::format("accepting client connections on port {}", m_config.clientListenServerConfig.port));
        started = true;
    }

    if (m_spInternalListenServer)
    {
        if (!m_spInternalListenServer->StartAccept())
        {
            LOG_WRITE(LogLevel::Error, "internal listen StartAccept failed");
            return false;
        }
        LOG_WRITE(LogLevel::Info, std::format("accepting internal server connections on port {}", m_config.internalListenServerConfig.port));
        started = true;
    }

    if (!started)
    {
        LOG_WRITE(LogLevel::Warn, "no listen NetServer configured");
        return false;
    }

    return true;
}

bool ServerBase::StartRegistryClient()
{
    if (!m_spRegistryClient)
    {
        LOG_WRITE(LogLevel::Warn, "no RegistryClient configured");
        return false;
    }

    m_spRegistryClient->Start();
    LOG_WRITE(LogLevel::Info, "started");
    return true;
}

void ServerBase::Run()
{
    LOG_WRITE(LogLevel::Info, "server is running. waiting for shutdown signal...");

    std::unique_lock<std::mutex> lock(m_shutdownMutex);
    m_shutdownCv.wait(lock, [this] { return !m_bRunning.load(); });

    LOG_WRITE(LogLevel::Info, "shutdown signal received. shutting down...");

    shutdownInternal();
}

void ServerBase::RequestShutdown()
{
    if (m_bShuttingDown.exchange(true))
        return;  // 이미 종료 진행 중

    LOG_WRITE(LogLevel::Info, "graceful shutdown requested");

    m_metricsRegistry.Set(GaugeMetric::ServerBase_Ready, 0.0);
    m_metricsRegistry.Set(GaugeMetric::ServerBase_ShuttingDown, 1.0);

    // 레지스트리에 종료 알림
    if (m_spRegistryClient && m_spRegistryClient->IsRegistered())
        m_spRegistryClient->SendShutdownNotify();

    // Listen 서버 Accept 중단 (신규 접속 차단)
    if (m_spClientListenServer)
    {
        m_spClientListenServer->StopAccept();
        LOG_WRITE(LogLevel::Info, "client listen accept stopped");
    }

    if (m_spInternalListenServer)
    {
        m_spInternalListenServer->StopAccept();
        LOG_WRITE(LogLevel::Info, "internal listen accept stopped");
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
    LOG_WRITE(LogLevel::Info, "begin");

    // 컨텐츠 스레드 정지
    for (auto& spThread : m_contentsThreads)
        spThread->Stop();

    m_contentsThreads.clear();
    LOG_WRITE(LogLevel::Info, "contents threads stopped");

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
    LOG_WRITE(LogLevel::Info, "server connections closed");

    // Listen NetServer 종료
    if (m_spClientListenServer)
    {
        m_spClientListenServer->Shutdown();
        m_spClientListenServer.reset();
        LOG_WRITE(LogLevel::Info, "client listen NetServer shutdown");
    }

    if (m_spInternalListenServer)
    {
        m_spInternalListenServer->Shutdown();
        m_spInternalListenServer.reset();
        LOG_WRITE(LogLevel::Info, "internal listen NetServer shutdown");
    }

    // DB 큐 종료 (남은 요청 처리 후 worker 스레드 종료)
    m_dbQueue.Close();
    LOG_WRITE(LogLevel::Info, "DB queue closed");

    // IoContext 종료 (Worker 스레드 종료)
    m_ioContext.Shutdown();
    LOG_WRITE(LogLevel::Info, "IoContext shutdown");

    // 서브클래스 훅
    OnShutdown();

    if (m_metricsHttpServer.IsRunning())
    {
        m_metricsHttpServer.Stop();
        LOG_WRITE(LogLevel::Info, "metrics HTTP server stopped");
    }

    LOG_WRITE(LogLevel::Info, "complete");

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
        LOG_WRITE(LogLevel::Error, std::format("Initialize failed for {}:{}", ip, port));
        return nullptr;
    }

    spClient->SetEventHandler(&handler);
    spClient->Connect(ip, port);  // 연결시도 시작. 연결 안되면 자동으로 재연결 시도함

    {
        std::lock_guard<std::mutex> lock(m_serverConnectionsMutex);
        m_serverConnections.push_back(spClient);
    }

    LOG_WRITE(LogLevel::Info, std::format("connecting to {}:{}", ip, port));
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
        LOG_WRITE(LogLevel::Error, std::format("invalid threadIndex: {}", threadIndex));
        return;
    }

    m_contentsThreads[threadIndex]->AddContents(std::move(spContents));
}

void ServerBase::RemoveContents(int32 threadIndex, ContentsPtr spContents)
{
    if (threadIndex < 0 || threadIndex >= static_cast<int32>(m_contentsThreads.size()))
    {
        LOG_WRITE(LogLevel::Error, std::format("invalid threadIndex: {}", threadIndex));
        return;
    }

    m_contentsThreads[threadIndex]->RemoveContents(std::move(spContents));
}

db::IResumeExecutor* ServerBase::GetContentsThreadExecutor(int32 threadIndex)
{
    if (threadIndex < 0 || threadIndex >= static_cast<int32>(m_contentsThreads.size()))
        return nullptr;
    return m_contentsThreads[threadIndex]->GetResumeExecutor();
}

} // namespace serverbase
