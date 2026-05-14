#include "pch.h"
#include "LoginServer.h"

static LoginServer* g_pServer = nullptr;

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
    serverbase::ConfigParser configParser;
    configParser.Load("LoginServer.ini");

    serverbase::ServerBaseConfig config;
    config.serverType = ServerType::Login;
    config.serverId = configParser.GetInt32("Server", "Id", 0);
    config.serverIp = configParser.GetString("Server", "IP", "0.0.0.0");
    config.serverPort = static_cast<uint16>(configParser.GetInt32("Server", "Port", 10010));

    config.registryIp = configParser.GetString("Registry", "IP", "127.0.0.1");
    config.registryPort = static_cast<uint16>(configParser.GetInt32("Registry", "Port", 10001));
    config.useRegistry = true;

    // 로그인서버는 게이트웨이 정보를 폴링한다
    config.pollTargetTypes   = { ServerType::Gateway };
    config.userCountReportMs = 0;   // 로그인서버는 접속자 수 보고 안 함

    config.ioContextConfig.numConcurrentThread = configParser.GetInt32("Network", "NumConcurrentThread", 0);
    config.ioContextConfig.numWorkerThread = configParser.GetInt32("Network", "NumWorkerThread", 0);
    config.ioContextConfig.initPacketSize = configParser.GetInt32("Network", "InitPacketSize", 512);
    config.ioContextConfig.maxPacketSize = configParser.GetInt32("Network", "MaxPacketSize", 65535);

    // 클라이언트가 직접 접속 (Listen 서버 사용)
    config.useListenServer = true;
    config.listenServerConfig.ip = config.serverIp;
    config.listenServerConfig.port = config.serverPort;

    // 컨텐츠 스레드
    config.numContentsThreads = configParser.GetInt32("Contents", "NumContentsThreads", 0);
    config.contentsUpdateMs = configParser.GetInt32("Contents", "ContentsUpdateMs", 50);

    // 로그
    config.logDir   = configParser.GetString("Log", "Dir", "Logs");
    config.logLevel = LogLevel::Debug;

    LoginServer server;
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
