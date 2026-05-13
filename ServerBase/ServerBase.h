#pragma once

#include "pch.h"
#include "Types.h"
#include "Timer.h"
#include "ConfigParser.h"
#include "ObjectIdGenerator.h"
#include "ContentsThread.h"
#include "RegistryClient.h"

#include "DBTask.h"

namespace serverbase
{

// ServerBase 초기화 파라미터
struct ServerBaseConfig
{
    // 서버
    ServerType serverType = ServerType::Unknown;
    int32 serverId = 0;   // 서버 ID (1~999)
    std::string serverIp;
    uint16 serverPort = 0;

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

    // Listen NetServer (다른 서버/클라이언트의 접속을 기다리는 서버가 사용)
    bool useListenServer = false;
    netlib::NetServerConfig listenServerConfig;

    // 컨텐츠 스레드
    int32 numContentsThreads = 0; // 0이면 컨텐츠 스레드 없음
    int64 contentsUpdateMs = 50;  // Update 주기 (ms)

    // 로그
    std::string logDir = "Logs";
    LogLevel logLevel = LogLevel::Debug;
};

// 코루틴이 await 후 재시작될 때 IOCP Worker 스레드에서 재시작되도록 IOCP message 큐에 함수를 post 해주는 컴포넌트
class CoroutineResumeExecutor : public db::IResumeExecutor
{
public:
    CoroutineResumeExecutor(netlib::IoContext& ioContext) : m_ioContext(ioContext) {}
    void Post(std::function<void()> fn) override { m_ioContext.Post(std::move(fn)); }
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

        spPacket->SetHeader(
            static_cast<uint16>(totalSize),
            static_cast<uint16>(packetId),
            netlib::PacketFlags::None
        );

        return spPacket;
    }

    // netlib::Packet을 protobuf message로 역직렬화
    template<typename TMsg>
    bool DeserializePacket(const netlib::Packet& packet, TMsg& outMsg)
    {
        return packet::ProtoSerializer::Deserialize(
            packet.GetPayload(),
            packet.GetPayloadSize(),
            outMsg
        );
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
    // useListenServer=true인 서버는 반드시 override하여 자신의 FuncEventHandler를 반환해야 한다.
    // 서버가 클라이언트 접속과 다른서버 접속을 둘다 받아야 하는 경우, 클라패킷인지 서버패킷인지는 FuncEventHandler 내에서 구분해서 처리해야 한다.
    virtual netlib::FuncEventHandler* GetListenEventHandler() { return nullptr; }

private:
    void shutdownInternal();

protected:
    ServerBaseConfig m_config;
    int32 m_serverId = 0;

    netlib::IoContext m_ioContext; // IOCP + 워커스레드
    netlib::NetServerUPtr m_spListenServer; // Listen 서버

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
