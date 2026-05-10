#pragma once

#include "pch.h"
#include "ServerEntry.h"

// 레지스트리 서버는 모든 서버의 연결상태를 관리하는 서버이다.
// 모든 서버들은 가동되었을 때 레지스트리 서버에 접속하여 자신의 정보를 등록한다. 
// 등록된 서버의 ServerID, IP:Port 충돌 여부를 검증하여 충돌시 알려준다.
// 모든 서버는 레지스트리 서버에 다른 서버의 정보를 주기적으로 폴링하여 얻는다.
// 레지스트리 서버는 다른 서버의 연결상태를 하트비트로 체크한다.
class RegistryServer : public serverbase::ServerBase
{
public:
    RegistryServer() = default;
    ~RegistryServer() override = default;

    RegistryServer(const RegistryServer&) = delete;
    RegistryServer& operator=(const RegistryServer&) = delete;

protected:
    // ServerBase 훅
    bool OnInitialize() override;
    void OnBeforeShutdown() override;
    netlib::FuncEventHandler* GetListenEventHandler() override { return &m_listenEventHandler; }

private:
    // 네트워크 이벤트 핸들러
    bool onAccept(netlib::ISessionPtr spSession);
    void onConnect(netlib::ISessionPtr spSession);
    void onRecv(netlib::ISessionPtr spSession, netlib::PacketPtr spPacket);
    void onDisconnect(netlib::ISessionPtr spSession);

    // 패킷 핸들러
    void handleRegisterReq(netlib::ISessionPtr spSession, const netlib::PacketPtr& spPacket);
    void handleHeartbeatRes(netlib::ISessionPtr spSession, const netlib::PacketPtr& spPacket);
    void handlePollReq(netlib::ISessionPtr spSession, const netlib::PacketPtr& spPacket);
    void handleUserCountNtf(netlib::ISessionPtr spSession, const netlib::PacketPtr& spPacket);
    void handleShutdownReq(netlib::ISessionPtr spSession, const netlib::PacketPtr& spPacket);

private:
    // 등록 요청의 serverId, IP:Port 충돌 여부를 검증한다.
    // 충돌 없으면 true, 충돌 시 outErrorMsg에 이유를 담아 false 반환
    bool validateRegistration(int32 serverId, ServerType type, const std::string& ip, uint16 port, std::string& outErrorMsg);

    // 등록된 서버 정보를 현재 연결된 모든 서버에 브로드캐스트한다.
    void broadcastServerInfo(const ServerEntry& serverEntry);

    // 특정 세션에 서버 정보 1건을 전송한다.
    void sendServerInfoNtf(netlib::ISessionPtr spSession, const ServerEntry& serverEntry);

    // 하트비트
    void sendHeartbeatToAll();
    void checkHeartbeatTimeout();

    // 세션 ID로 serverId를 찾는다. 없으면 0 반환
    int32 findServerIdBySessionId(int64 sessionId);

private:
    mutable std::shared_mutex m_serverEntriesMutex;
    std::unordered_map<int32, ServerEntry> m_serverEntries; // 서버정보 목록. Key=serverId, Value=서버정보

    mutable std::shared_mutex m_sessionIndexMutex;
	std::unordered_map<int64, int32> m_sessionToServerId; // sessionId -> serverId 역방향 인덱스(빠른 조회용). Key=sessionId, Value=serverId

    // 검증용 DB 데이터 (TODO: RegistryDB 연동)
    mutable std::mutex m_registrationMutex;
    std::unordered_map<int32, std::string> m_idToEndpoint;   // Key=serverId, Value="서버타입:ip:port" (ID 중복 검증용)
    std::unordered_map<std::string, int32> m_endpointToId;   // Key="서버타입:ip:port", Value=serverId (IP:Port 중복 검증용)

    // 하트비트 타이머 ID
    int32 m_heartbeatSendTimerId = -1;
    int32 m_heartbeatCheckTimerId = -1;

    static constexpr int64  k_heartbeatIntervalMs = 30000;  // 30초마다 요청
    static constexpr int64  k_heartbeatTimeoutMs  = 60000;  // 1분 무응답 시 끊김

    netlib::FuncEventHandler m_listenEventHandler;
};
