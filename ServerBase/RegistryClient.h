#pragma once

#include "pch.h"
#include "Types.h"
#include "Timer.h"

namespace serverbase
{

// 레지스트리 서버와의 통신을 담당하는 클래스
// 레지스트리 서버에 접속한다. 처음부터 접속에 실패했으면 10초마다 재연결, 접속성공후 연결이 끊긴 경우는 1분마다 재연결한다.
// 레지스트리 서버에 자신의 서버 정보 등록, 하트비트 응답, 1분마다 다른서버 정보 폴링, 접속자 수 보고, Graceful Shutdown 알림 등을 담당한다.
class RegistryClient : public netlib::INetEventHandler
{
public:
    // 레지스트리 서버로부터 서버 정보 통보를 받았을 때 호출되는 콜백 타입
    using ServerInfoCallback = std::function<void(const ServerInfo&)>;

    // 등록이 거부되었을 때 호출되는 콜백 타입
    // 서버는 이 콜백을 받으면 즉시 종료해야 한다 (config의 서버ID 충돌 등)
    using RegisterRejectedCallback = std::function<void(const std::string& reason)>;

public:
    RegistryClient() = default;
    ~RegistryClient() override;

    RegistryClient(const RegistryClient&) = delete;
    RegistryClient& operator=(const RegistryClient&) = delete;

public:
    struct Config
    {
        std::string  registryIp;
        uint16       registryPort        = 0;

        ServerType   myServerType        = ServerType::Unknown;
        int32        myServerId          = 0; 
        std::string  myIp;
        uint16       myPort              = 0;

        // 폴링할 서버 타입 목록. 각 서버가 자신에게 필요한 타입만 등록한다.
        std::vector<ServerType> pollTargetTypes;

        int64 pollIntervalMs       = 60000;   // 서버 목록 폴링 주기 (기본 1분)
        int64 userCountReportMs    = 60000;   // 접속자 수 보고 주기 (기본 1분, 0이면 보고 안 함)

        // 재연결 주기 
        int32 initialReconnectMs   = 10000;   // 첫 부팅 시 10초
        int32 runningReconnectMs   = 60000;   // 운영 중 1분
    };

public:
    // 초기화. IoContext는 ServerBase에서 관리하는 것을 공유한다.
    bool Initialize(netlib::IoContext* pIoContext, const Config& config);

    // 레지스트리 서버 연결시작
    void Start();

    void Stop();

    bool IsRegistered() const { return m_bRegistered.load(); }
    int32 GetServerId() const { return m_config.myServerId; }

    // 다른 서버 정보가 갱신될 때 호출될 콜백 등록
    void SetServerInfoCallback(ServerInfoCallback callback);

    // 등록 거부 시 호출될 콜백 등록
    void SetRegisterRejectedCallback(RegisterRejectedCallback callback);

    // 접속자 수 업데이트
    void SetUserCount(int32 count) { m_userCount.store(count); }

    // Graceful Shutdown 알림을 레지스트리 서버로 전송
    void SendShutdownNotify();

    // 알려진 서버 목록 조회
    std::vector<ServerInfo> GetServerList(ServerType type) const;

public:
    // INetEventHandler
    bool OnAccept   (netlib::ISessionPtr spSession)                            override;
    void OnConnect  (netlib::ISessionPtr spSession)                            override;
    void OnRecv     (netlib::ISessionPtr spSession, netlib::PacketPtr spPacket) override;
    void OnDisconnect(netlib::ISessionPtr spSession)                           override;
    void OnError    (netlib::ISessionPtr spSession, const std::string& msg)    override;

private:
    void sendRegisterReq();
    void sendPollReq();
    void sendHeartbeatRes(int64 timestampMs);
    void sendUserCountReport();
    void sendPacket(netlib::PacketPtr spPacket);  // 현재 세션으로 패킷 전송

    void handleRegisterRes (const netlib::Packet& packet);
    void handleServerInfoNtf(const netlib::Packet& packet);
    void handlePollRes      (const netlib::Packet& packet);
    void handleHeartbeatReq (const netlib::Packet& packet);

    void applyServerInfo(const ServerInfo& info);

private:
    Config m_config;
    netlib::NetClientUPtr m_upNetClient;

    std::atomic<bool> m_bRegistered { false };
    std::atomic<int32> m_userCount { 0 };

    // 현재 연결된 레지스트리 세션 (OnConnect에서 저장, OnDisconnect에서 제거)
    std::mutex m_sessionMutex;
    netlib::ISessionPtr m_spSession;

    // 알려진 서버 목록
    mutable std::shared_mutex m_serversMutex;
    std::unordered_map<int32, ServerInfo> m_servers;   // key = serverId

    // 타이머 (폴링, 접속자 수 보고)
    Timer m_timer;
    int32 m_pollTimerId = -1;
    int32 m_userCountTimerId = -1;

    ServerInfoCallback m_serverInfoCallback;
    RegisterRejectedCallback m_registerRejectedCallback;
};

using RegistryClientPtr = std::shared_ptr<RegistryClient>;
using RegistryClientWPtr = std::weak_ptr<RegistryClient>;
using RegistryClientUPtr = std::unique_ptr<RegistryClient>;

} // namespace serverbase
