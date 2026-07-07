#pragma once

#include "pch.h"
#include "Types.h"
#include "Timer.h"
#include "ConfigParser.h"
#include "ObjectIdGenerator.h"
#include "ContentsThread.h"
#include "RegistryClient.h"
#include "ThreadSafeUnorderedMap.h"
#include "DBConnectorLib.h"  

namespace serverbase
{

// ServerBase 초기화 파라미터
struct ServerBaseConfig
{
    // 서버
    ServerType serverType = ServerType::Unknown;
    int32 serverId = 0;   // 서버 ID (1~999)
    std::string privateIp;   // 서버간 내부통신 및 리슨소켓 bind 주소 (VPC 프라이빗 IP). 필수, 시작 시 검증됨
    std::string publicIp;    // 클라이언트용 외부접속 주소 (Elastic IP)

    // 레지스트리 서버 접속정보
    std::string registryIp;
    uint16 registryPort = 0;
    bool useRegistry = true;   // 레지스트리 서버 자신은 false로 설정

    // 서버정보 폴링대상 서버목록
    std::vector<ServerType> pollTargetTypes;

    // 접속자 수 보고 주기(ms). 0이면 보고 안 함
    int64 userCountReportMs = 0;

    // IoContext 설정(IOCP + 워커 스레드)
    netlib::IoContextConfig ioContextConfig;

    // 클라이언트 접속을 받는 Listen NetServer
    bool useClientListenServer = false;
    netlib::NetServerConfig clientListenServerConfig;

    // 내부 서버(게임서버, 로그인서버 등) 접속을 받는 Listen NetServer
    bool useInternalListenServer = false;
    netlib::NetServerConfig internalListenServerConfig;

    // 컨텐츠 스레드
    int32 numContentsThreads = 0; // 0이면 컨텐츠 스레드 없음
    int64 contentsUpdateMs = 50;  // Update 주기 (ms)

    // 로그
    std::string logDir = "Logs";
    LogLevel logLevel = LogLevel::Debug;

    // ── DB (선택) ───────────────────────────────────────────────
    // 서버의 main 함수에서 LoadDBConfigFromIni()로 채운다. ServerBase가 Initialize 때 알아서 연결한다.
    //   - [AccountDB] Host 있으면 useAccountDB=true (Host/Port/User/Password/DBName)
    //   - [GameDB] Enabled=true 면 useGameDB=true. 샤드 목록은 AccountDB의 GameDBRegistry에서 읽고,
    //     샤드 접속 자격증명(user/password)은 AccountDB와 공유한다. (AccountDB 필수)
    bool useAccountDB = false;
    db::DBConnectionConfig accountDBConfig;
    bool useGameDB = false;
    int  dbNumWorkers = 8;   // 공유 워커 스레드 수 (각 DB는 이 수만큼 커넥션을 연다)
};

// ini의 [AccountDB]/[GameDB] 섹션을 읽어 ServerBaseConfig의 DB 필드를 채운다.
// (각 서버 main에서 ServerBaseConfig를 만들 때 호출)
void LoadDBConfigFromIni(ServerBaseConfig& config, ConfigParser& parser);

// 코루틴이 await 후 재시작될 때 IOCP Worker 스레드에서 재시작되도록 IOCP message 큐에 함수를 post 해주는 컴포넌트
class CoroutineResumeExecutor : public db::IResumeExecutor
{
public:
    CoroutineResumeExecutor(netlib::IoContext& ioContext) : m_ioContext(ioContext) {}
    void Post(std::function<void()> fn) override { m_ioContext.PostMsg(std::move(fn)); }
private:
    netlib::IoContext& m_ioContext;
};

// ServerBase 클래스는 모든 서버(레지스트리, 로그인, 게이트웨이, 게임 등)가 상속하는 기반 클래스이다.
// IoContext(IOCP + worker 스레드), Listen용 NetServer, 서버간 connect용 NetClient, 레지스트리 서버와 통신하는 RegistryClient를 관리한다.
// 그리고 컨텐츠 스레드를 관리하며 컨텐츠 스레드내의 컨텐츠를 주기적으로 업데이트 한다.
class ServerBase
{
public:
    ServerBase()          = default;
    virtual ~ServerBase() = default;

    ServerBase(const ServerBase&)            = delete;
    ServerBase& operator=(const ServerBase&) = delete;

public:
    bool Initialize(const ServerBaseConfig& config);

    // Listen 서버의 Accept 시작
    bool StartAccept();

    // RegistryClient 시작
    bool StartRegistryClient();

    // 메인 루프. RequestShutdown()이 호출될 때까지 block됨
    void Run();

    // Graceful Shutdown 요청
    // 레지스트리 서버에 종료 알림, Accept 중단, m_bRunning 플래그 해제
    void RequestShutdown();

    // ── 다른 서버와 연결관리 ─────────────────────────────────────

	// 다른 서버와 연결한다. 내부적으로 NetClient을 생성하여 연결한다. NetClient는 ServerBase가 관리한다.
	// 연결을 끊으려면 DisconnectToServer 사용
	netlib::NetClientPtr ConnectToServer(const std::string& ip, uint16 port, netlib::FuncEventHandler& handler);

    // 다른 서버와의 연결을 끊는다.
    void DisconnectToServer(netlib::NetClientPtr spClient);


    // ── 컨텐츠 스레드 ───────────────────────────────────────────────────

    // 컨텐츠 스레드에 컨텐츠를 배정, 제거
    void AssignContents(int32 threadIndex, ContentsPtr spContents);
    void RemoveContents(int32 threadIndex, ContentsPtr spContents);

    int32 GetContentsThreadCount() const { return static_cast<int32>(m_contentsThreads.size()); }

    // 컨텐츠 스레드의 코루틴 resume executor 를 반환한다.
    // 해당 컨텐츠 스레드에서 시작한 DB 코루틴의 후속작업을 그 스레드에서 재개시키고 싶을 때
    // ExecuteAsync 의 executor 인자로 넘긴다. 범위 밖 인덱스면 nullptr.
    db::IResumeExecutor* GetContentsThreadExecutor(int32 threadIndex);

    // ── Getters ───────────────────────────────────────────────────
    int32      GetServerId()    const { return m_serverId; }
    ServerType GetServerType()  const { return m_config.serverType; }
    bool       IsRunning()      const { return m_bRunning.load(); }
    bool       IsShuttingDown() const { return m_bShuttingDown.load(); }

    int64 GenerateObjectId() { return m_objectIdGenerator.Generate(); }

    Timer&            GetTimer()          { return m_timer; }
    RegistryClient*   GetRegistryClient() { return m_spRegistryClient.get(); }
    netlib::PacketPtr AllocPacket()       { return m_ioContext.GetPacketPool().Alloc(); }
    netlib::IoContext& GetIoContext()     { return m_ioContext; }

    // co_await ExecuteAsync() 호출 시 pExecutor 인자에 전달한다.
    // DB worker 완료 후 코루틴을 IOCP Worker 스레드에서 resume한다.
    db::IResumeExecutor* GetCoroutineResumeExecutor()   { return &m_coroutineResumeExecutor; }

    // 서버의 단일 DB 큐. AccountDB/GameDB 샤드를 모두 관리한다(ServerBaseConfig에 따라 Initialize 때 자동 연결).
    //   GetDB().ExecuteAsync(db::EDBType::Account, 0, ...) 또는 (db::EDBType::Game, gameDbIndex, ...)
    db::AsyncDBQueue&       GetDB()       { return m_dbQueue; }
    const db::AsyncDBQueue& GetDB() const { return m_dbQueue; }

public:
    // ── 패킷 ───────────────────────────────────────────────────

    // protobuf message를 netlib::Packet으로 직렬화
    template<typename TPacketId, typename TMsg>
    netlib::PacketPtr SerializePacket(TPacketId packetId, const TMsg& msg)
    {
        int32 payloadSize = packet::ProtoSerializer::GetPayloadSize(msg);
        int32 totalSize = static_cast<int32>(sizeof(netlib::PacketHeader)) + payloadSize;

        netlib::PacketPtr spPacket = m_ioContext.GetPacketPool().Alloc(totalSize);
        if (!spPacket)
            return nullptr;

        if (payloadSize > 0)
        {
            if (!packet::ProtoSerializer::Serialize(msg, spPacket->GetPayload(), payloadSize))
                return nullptr;
        }

        spPacket->SetHeader(static_cast<uint16>(totalSize), static_cast<uint16>(packetId), netlib::PacketFlags::None);

        return spPacket;
    }

    // netlib::Packet을 protobuf message로 역직렬화
    template<typename TMsg>
    bool DeserializePacket(const netlib::Packet& packet, TMsg& outMsg)
    {
        return packet::ProtoSerializer::Deserialize(packet.GetPayload(), packet.GetPayloadSize(), outMsg);
    }

protected:
    // ── 서브클래스 hook 함수들 ───────────────────────────

    // Initialize 완료 직후 호출됨. 서브클래스 초기화, 핸들러 콜백 등록
    virtual bool OnInitialize() { return true; }

    // 다른 서버 정보가 갱신됐을 때 호출됨. 서버간 연결 시작/해제 로직을 여기에 구현
    virtual void OnServerInfoUpdated(const ServerInfo& info) {}

    // RequestShutdown 호출 시 호출됨
    virtual void OnBeforeShutdown() {}

    // 모든 정리 완료 후 호출됨
    virtual void OnShutdown() {}

    // Listen NetServer 이벤트 핸들러
    // useClientListenServer=true인 서버는 반드시 override하여 클라이언트용 FuncEventHandler를 반환해야 한다.
    virtual netlib::FuncEventHandler* GetClientListenEventHandler()   { return nullptr; }

    // useInternalListenServer=true인 서버는 반드시 override하여 내부서버용 FuncEventHandler를 반환해야 한다.
    virtual netlib::FuncEventHandler* GetInternalListenEventHandler() { return nullptr; }

private:
    void shutdownInternal();

    // ServerBaseConfig의 DB 설정에 따라 AccountDB/GameDB 샤드를 m_dbQueue에 연다. DB 안 쓰면 그냥 true.
    bool initializeDatabases();
    // AccountDB의 GameDBRegistry(status='active')를 읽어 GameDB 샤드 등록정보를 entries에 추가한다.
    bool loadGameShards(std::vector<db::AsyncDBQueue::OpenEntry>& entries);

protected:
    ServerBaseConfig m_config;
    int32 m_serverId = 0;

    // 서버의 모든 DB(AccountDB + GameDB 샤드)를 관리하는 단일 큐.
    db::AsyncDBQueue m_dbQueue;

    netlib::IoContext m_ioContext; // IOCP + 워커스레드
    netlib::NetServerUPtr m_spClientListenServer;   // 클라이언트 접속용 Listen 서버
    netlib::NetServerUPtr m_spInternalListenServer; // 내부 서버 접속용 Listen 서버

    std::mutex m_serverConnectionsMutex;
    std::vector<netlib::NetClientPtr> m_serverConnections; // 다른서버와 연결 관리

    RegistryClientUPtr m_spRegistryClient; // 레지스트리서버와의 연결 관리

    std::vector<ContentsThreadPtr> m_contentsThreads; // 컨텐츠 스레드

    Timer m_timer;
    ObjectIdGenerator m_objectIdGenerator;
    ConfigParser m_configFile;

    std::atomic<bool> m_bRunning { false };
    std::atomic<bool> m_bShuttingDown { false };
    std::mutex m_shutdownMutex;
    std::condition_variable m_shutdownCv;

    CoroutineResumeExecutor m_coroutineResumeExecutor { m_ioContext };  // co_await resume 전용 executor
};

} // namespace serverbase
