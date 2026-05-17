#include "pch.h"
#include "GameServer.h"

static GameServer* g_pServer = nullptr;

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
    // GameServer config 로드
    serverbase::ConfigParser configParser;
    configParser.Load("GameServer.ini");

    // ServerBaseConfig 만들기
    serverbase::ServerBaseConfig config;
    config.serverType = ServerType::Game;
    config.serverId   = configParser.GetInt32("Server", "Id", 200);
    config.serverIp   = configParser.GetString("Server", "IP", "0.0.0.0");

    config.registryIp   = configParser.GetString("Registry", "IP", "127.0.0.1");
    config.registryPort = static_cast<uint16>(configParser.GetInt32("Registry", "Port", 10001));
    config.useRegistry  = true;

    // 게임서버는 게이트웨이 정보를 폴링한다
    config.pollTargetTypes   = { ServerType::Gateway };
    config.userCountReportMs = configParser.GetInt32("Network", "UserCountReportMs", 10000);

    config.ioContextConfig.numConcurrentThread = configParser.GetInt32("Network", "NumConcurrentThread", 0);
    config.ioContextConfig.numWorkerThread     = configParser.GetInt32("Network", "NumWorkerThread", 0);
    config.ioContextConfig.initPacketSize      = configParser.GetInt32("Network", "InitPacketSize", 512);
    config.ioContextConfig.maxPacketSize       = configParser.GetInt32("Network", "MaxPacketSize", 65535);

    // 게임서버는 클라이언트와 직접 통신하지 않음
    config.useClientListenServer = false;

    // 내부 서버용 포트 (채팅서버 등이 게임서버로 connect)
    config.useInternalListenServer = true;
    config.internalListenServerConfig.ip   = config.serverIp;
    config.internalListenServerConfig.port = static_cast<uint16>(configParser.GetInt32("Server", "InternalPort", -1));

    // 컨텐츠 스레드
    // ini의 NumContentsThreads가 0이면 CPU 코어 수 - 1로 자동 설정
    // (서버구조개요.md 의도: 오픈필드 전용 스레드 1개 + 일반 컨텐츠 스레드 여러 개)
    config.numContentsThreads = configParser.GetInt32("Contents", "NumContentsThreads", 0);
    if (config.numContentsThreads <= 0)
    {
        const int32 coreCount = static_cast<int32>(std::thread::hardware_concurrency());
        config.numContentsThreads = (coreCount > 1) ? (coreCount - 1) : 1;
    }
    config.contentsUpdateMs   = configParser.GetInt32("Contents", "ContentsUpdateMs", 50);

    // 로그
    config.logDir   = configParser.GetString("Log", "Dir", "Logs");
    config.logLevel = Logger::StringToLogLevel(configParser.GetString("Log", "Level", "Debug"));

    // 서버 시작
    GameServer server;
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
