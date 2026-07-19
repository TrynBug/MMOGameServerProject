
#include "pch.h"
#include "CommunicationServer.h"

static CommunicationServer* g_pServer = nullptr;

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
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    serverbase::ConfigParser configParser;
    configParser.Load("CommunicationServer.ini");

    serverbase::ServerBaseConfig config;
    config.serverType = ServerType::Communication;
    config.serverId = configParser.GetInt32("Server", "Id", 20);
    config.privateIp = configParser.GetString("Server", "PrivateIP", "");
    config.publicIp = configParser.GetString("Server", "PublicIP", "");
    config.registryIp = configParser.GetString("Registry", "IP", "127.0.0.1");
    config.registryPort = static_cast<uint16>(configParser.GetInt32("Registry", "Port", 10001));
    config.useRegistry = true;
    config.pollTargetTypes = { ServerType::Game };
    config.userCountReportMs = 0;
    config.ioContextConfig.numConcurrentThread = configParser.GetInt32("Network", "NumConcurrentThread", 0);
    config.ioContextConfig.numWorkerThread = configParser.GetInt32("Network", "NumWorkerThread", 0);
    config.ioContextConfig.initPacketSize = configParser.GetInt32("Network", "InitPacketSize", 512);
    config.ioContextConfig.maxPacketSize = configParser.GetInt32("Network", "MaxPacketSize", 65535);
    config.useClientListenServer = false;
    config.useInternalListenServer = false;
    config.numContentsThreads = configParser.GetInt32("Contents", "NumContentsThreads", 1);
    config.contentsUpdateMs = configParser.GetInt32("Contents", "ContentsUpdateMs", 50);
    config.logDir = configParser.GetString("Log", "Dir", "Logs");
    config.logLevel = Logger::StringToLogLevel(configParser.GetString("Log", "Level", "Debug"));

    CommunicationServer server;
    g_pServer = &server;
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);

    if (!server.Initialize(config) || !server.StartRegistryClient())
        return -1;

    server.Run();
    return 0;
}
