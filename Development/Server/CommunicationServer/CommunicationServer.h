#pragma once

#include "pch.h"

struct CharacterChatPresence
{
    int64 characterId = 0;
    int32 gameServerId = 0;
};

// CommunicationServer는 GameServer들과의 내부 연결을 관리한다.
// 전체 채팅 fan-out과 캐릭터 presence 기반 귓속말 라우팅을 담당한다.
class CommunicationServer : public serverbase::ServerBase
{
public:
    CommunicationServer() = default;
    ~CommunicationServer() override = default;

    CommunicationServer(const CommunicationServer&) = delete;
    CommunicationServer& operator=(const CommunicationServer&) = delete;

protected:
    bool OnInitialize() override;
    void OnMetricsCollect() override;
    void OnServerInfoUpdated(const ServerInfo& info) override;
    void OnBeforeShutdown() override;

private:
    // Registry에서 전달받은 GameServer의 internal endpoint에 연결/해제한다.
    void connectToGameServer(int32 gameServerId, const std::string& ip, uint16 port);
    void disconnectFromGameServer(int32 gameServerId);

    // CommunicationServer가 connector이므로 연결 직후 handshake req를 전송한다.
    void onGameServerConnect(const netlib::ISessionPtr& spSession);
    void onGameServerRecv(const netlib::ISessionPtr& spSession, const netlib::PacketPtr& spPacket);
    void onGameServerDisconnect(const netlib::ISessionPtr& spSession);
    void handleGameServerHandshakeRes(const netlib::ISessionPtr& spSession, const ServerPacket::ServerHandshakeRes& msg);

    // handshake 완료 후 GameServer에서 들어오는 채팅 패킷 처리.
    void handleGameServerChatBroadcastReq(const netlib::ISessionPtr& spSession, const ServerPacket::ChatBroadcastReq& msg);
    void handleGameServerChatPresenceNtf(const netlib::ISessionPtr& spSession, const ServerPacket::ChatPresenceNtf& msg);
    void handleGameServerWhisperReq(const netlib::ISessionPtr& spSession, const ServerPacket::WhisperReq& msg);
    void sendWhisperRes(const netlib::ISessionPtr& spSession, const ServerPacket::WhisperReq& req, bool success, const std::string& errorMsg);

private:
    netlib::FuncEventHandler m_gameServerEventHandler;
    serverbase::PacketDispatcher m_gameServerDispatcher;

    // handshake가 완료된 GameServer 세션. 이후 전역 채팅 fan-out 등의 송신 경로가 된다.
    SharedThreadSafeUnorderedMap<int32, netlib::ISessionPtr> m_safeGameServerSessions;

    // NetClient는 자동 재접속 수명을 유지하고, ServerInfo는 handshake 응답의 serverId를 검증한다.
    ExclusiveThreadSafeUnorderedMap<int32, netlib::NetClientPtr> m_safeGameServerClients;
    SharedThreadSafeUnorderedMap<int32, ServerInfo> m_safeGameServerInfos;

    // 캐릭터 이름은 전역 유일하다는 전제다. 중복 생성 방지는 캐릭터 생성 정책에서 별도로 처리한다.
    SharedThreadSafeUnorderedMap<std::string, CharacterChatPresence> m_safeCharacterPresences;

};
