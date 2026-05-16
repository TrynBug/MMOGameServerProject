#include "pch.h"
#include "GatewayServer.h"

static GatewayServer* g_pServer = nullptr;

// 프로그램 강제종료 시 서버를 정상종료하고 종료되도록 하는 이벤트핸들러
BOOL WINAPI consoleCtrlHandler(DWORD ctrlType)
{
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_CLOSE_EVENT)
    {
        if (g_pServer)
            g_pServer->RequestShutdown();

        return TRUE;
    }
    return FALSE;
}

int main()
{
    // Gateway 서버 config 로드
    serverbase::ConfigParser configParser;
    configParser.Load("GatewayServer.ini");

    // ServerBaseConfig 만들기
    serverbase::ServerBaseConfig config;
    config.serverType = ServerType::Gateway;
    config.serverId = configParser.GetInt32("Server", "Id", -1);
    config.serverIp = configParser.GetString("Server", "IP", "0.0.0.0");

    config.registryIp = configParser.GetString("Registry", "IP", "127.0.0.1");
    config.registryPort = static_cast<uint16>(configParser.GetInt32("Registry", "Port", 10001));
    config.useRegistry = true;

    // 게이트웨이서버는 로그인서버, 게임서버 정보를 폴링
    config.pollTargetTypes = { ServerType::Login, ServerType::Game };
    config.userCountReportMs = configParser.GetInt32("Network", "UserCountReportMs", 10000);

    config.ioContextConfig.numConcurrentThread = configParser.GetInt32("Network", "NumConcurrentThread", 0);
    config.ioContextConfig.numWorkerThread = configParser.GetInt32("Network", "NumWorkerThread", 0);
    config.ioContextConfig.initPacketSize = configParser.GetInt32("Network", "InitPacketSize", 512);
    config.ioContextConfig.maxPacketSize = configParser.GetInt32("Network", "MaxPacketSize", 65535);

    // 클라이언트용 포트 (외부 인터넷에서 접속)
    config.useClientListenServer = true;
    config.clientListenServerConfig.ip   = config.serverIp;
    config.clientListenServerConfig.port = static_cast<uint16>(configParser.GetInt32("Server", "ClientPort", -1));;

    // 내부 서버용 포트 (게임서버/로그인서버 접속. 방화벽으로 외부 차단 권장)
    config.useInternalListenServer = true;
    config.internalListenServerConfig.ip   = config.serverIp;
    config.internalListenServerConfig.port = static_cast<uint16>(configParser.GetInt32("Server", "InternalPort", -1));

    // 컨텐츠 스레드
    config.numContentsThreads = configParser.GetInt32("Contents", "NumContentsThreads", 0);
    config.contentsUpdateMs = configParser.GetInt32("Contents", "ContentsUpdateMs", 50);

    // 로그
    config.logDir = configParser.GetString("Log", "Dir", "Logs");
    config.logLevel = Logger::StringToLogLevel(configParser.GetString("Log", "Level", "Debug"));

    // 서버 시작
    GatewayServer server;
    g_pServer = &server;

    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);

    if (!server.Initialize(config))
        return -1;

    if (!server.StartRegistryClient())
        return -1;

    if (!server.StartAccept())
        return -1;

    server.Run();
    return 0;
}
