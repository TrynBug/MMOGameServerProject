#include "pch.h"
#include "RegistryServer.h"

static RegistryServer* g_pServer = nullptr;

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
	// registry 서버 config 로드
    serverbase::ConfigParser configParser;
    configParser.Load("RegistryServer.ini");

    // ServerBaseConfig 만들기
    serverbase::ServerBaseConfig config;
    config.serverType = ServerType::Registry;
    config.serverId = configParser.GetInt32("Server", "Id", 1);
    config.serverIp = configParser.GetString("Server", "IP", "0.0.0.0");
    config.serverPort = static_cast<uint16>(configParser.GetInt32("Server", "Port", 10001));

    // 레지스트리 서버는 자신이 레지스트리이므로 RegistryClient 불필요
    config.useRegistry = false;

    config.ioContextConfig.numConcurrentThread = configParser.GetInt32("Network", "NumConcurrentThread", 2);
    config.ioContextConfig.numWorkerThread = configParser.GetInt32("Network", "NumWorkerThread", 4);
    config.ioContextConfig.initPacketSize = configParser.GetInt32("Network", "InitPacketSize", 512);
    config.ioContextConfig.maxPacketSize = configParser.GetInt32("Network", "MaxPacketSize", 65535);

    // Listen 서버 사용 (다른 서버들이 이 서버로 접속)
    config.useListenServer = true;
    config.listenServerConfig.ip = config.serverIp;
    config.listenServerConfig.port = config.serverPort;

    // 컨텐츠 스레드
    config.numContentsThreads = configParser.GetInt32("Contents", "NumContentsThreads", 0);
    config.contentsUpdateMs = configParser.GetInt32("Contents", "ContentsUpdateMs", 50);

    // 로그
    config.logDir = configParser.GetString("Log", "Dir", "Logs");
    config.logLevel = Logger::StringToLogLevel(configParser.GetString("Log", "Level", "Debug"));

    // 서버 시작
    RegistryServer server;
    g_pServer = &server;

    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);

    if (!server.Initialize(config))
        return -1;

    if (!server.StartAccept())
        return -1;

    server.Run();

    return 0;
}
