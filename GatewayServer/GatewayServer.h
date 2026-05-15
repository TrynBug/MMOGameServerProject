#pragma once

#include "pch.h"
#include "GatewayServerDefine.h"
#include "GatewayUser.h"
#include "ThreadSafeUnorderedMap.h"

// 게이트웨이서버는 클라이언트가 게임플레이를위해 통신하는 서버이다.
// 클라이언트는 게이트웨이서버에 접속하여 통신하고, 게이트웨이서버는 클라이언트에게 받은 패킷을 게임서버에 전달한다(GatewayToGamePacketNtf). 
// 그리고 게임서버에서 받은 패킷을 클라이언트에게 전달한다(GameToGatewayPacketNtf / BroadcastNtf).
// 클라이언트가 게이트웨이서버에 처음 접속할 때는 로그인서버에서 받은 인증토큰을 검증한다.
// 클라이언트의 이전 접속 게임서버 정보를 관리하여, 클라이언트가 빠른시간내에 재접속 시 이전에 접속했던 게임서버로 접속하게 한다.
// 게임서버 요청에 의한 유저 게임서버 간 이동 처리
class GatewayServer : public serverbase::ServerBase
{
public:
    GatewayServer() = default;
    ~GatewayServer() override = default;

    GatewayServer(const GatewayServer&) = delete;
    GatewayServer& operator=(const GatewayServer&) = delete;

protected:
    // ServerBase 훅
    bool OnInitialize() override;
    void OnServerInfoUpdated(const ServerInfo& info) override;
    void OnBeforeShutdown() override;
    netlib::FuncEventHandler* GetClientListenEventHandler() override { return &m_clientListenEventHandler; }
    netlib::FuncEventHandler* GetInternalListenEventHandler() override { return &m_internalListenEventHandler; }

private:
    // ── 네트워크 이벤트 (클라이언트 포트) ────────────────────────────────
    bool onClientAccept(const netlib::ISessionPtr& spSession);
    bool onClientRecv  (const netlib::ISessionPtr& spSession, const netlib::PacketPtr& spPacket);
    void onClientDisconnect(const netlib::ISessionPtr& spSession);

    // ── 네트워크 이벤트 (내부 서버 포트) ─────────────────────────────────
    bool onInternalAccept(const netlib::ISessionPtr& spSession);
    bool onInternalRecv  (const netlib::ISessionPtr& spSession, const netlib::PacketPtr& spPacket);
    void onInternalDisconnect(const netlib::ISessionPtr& spSession);

    // ── 클라이언트 패킷 핸들러 ───────────────────────────────────────────
    void handleClientPacket(const netlib::ISessionPtr& spSession, netlib::PacketPtr spPacket);

    // 클라이언트 최초 접속 시 인증토큰 검증 패킷
    // 문서상 별도 패킷 ID는 없으나 게이트웨이가 처음 받는 패킷을 인증 패킷으로 처리
    void handleAuthReq     (const netlib::ISessionPtr& spSession, netlib::PacketPtr spPacket);

    // 인증 완료 후 일반 클라이언트 패킷 → 게임서버로 relay
    void relayToGameServer (const netlib::ISessionPtr& spSession, netlib::PacketPtr spPacket);

    // 로그아웃 요청 처리
    void handleLogoutReq   (const netlib::ISessionPtr& spSession);

    // ── 게임서버 패킷 핸들러 ─────────────────────────────────────────────
    void handleGameServerPacket(const netlib::ISessionPtr& spSession, netlib::PacketPtr spPacket);

    void handleGameServerHandshake  (const netlib::ISessionPtr& spSession, const ServerPacket::GameServerHandshakeNtf&    msg);
    void handleGameToGatewayPacket  (const netlib::ISessionPtr& spSession, const ServerPacket::GameToGatewayPacketNtf&    msg);
    void handleGameToGatewayBroadcast(const netlib::ISessionPtr& spSession, const ServerPacket::GameToGatewayBroadcastNtf& msg);
    void handleUserMoveToGameServer (const netlib::ISessionPtr& spSession, const ServerPacket::UserMoveToGameServerReq&   msg);

    // ── 로그인서버 패킷 핸들러 ───────────────────────────────────────────
    void handleLoginServerPacket    (const netlib::ISessionPtr& spSession, netlib::PacketPtr spPacket);
    void handleLoginAuthTokenNtf    (const netlib::ISessionPtr& spSession, const ServerPacket::LoginAuthTokenNtf&   msg);
    void handleLoginDuplicateNtf    (const netlib::ISessionPtr& spSession, const ServerPacket::LoginDuplicateNtf&   msg);


private:
    // 세션에서 SessionMetaInfo를 꺼낸다.
    static SessionMetaInfo* getSessionMeta(const netlib::ISessionPtr& spSession);

    // 로그인서버로부터 사전 전달받은 인증토큰 저장
    void  storeAuthToken(int64 userId, uint64 authToken, int64 expireTimeMs);
    // 토큰 검증. 성공 시 true 반환하고 내부 맵에서 제거
    bool  consumeAuthToken(int64 userId, uint64 authToken);
    // 만료된 토큰 정리
    void  cleanupExpiredTokens();

    void upsertPrevGameServer(int64 userId, int32 gameServerId);
    void cleanupExpiredPrevGameServer();

    // 클라가 연결될 게임서버 선택 (로드밸런싱)
    // 이전 접속 게임서버 정보가 있으면 우선 선택, 없으면 유저 수가 적은 서버 선택
    std::optional<ServerInfo> selectGameServer(int64 userId) const;

    // 게임서버에 패킷 전송 헬퍼
    void sendToGameServer(int32 gameServerId, netlib::PacketPtr spPacket);

    // 유저에게 강제 종료 알림 전송 후 Disconnect
    void forceDisconnectUser(int64 userId, const std::string& reason);

private:
    SharedThreadSafeUnorderedMap<int64, AuthTokenEntry> m_safeAuthTokens;   // key=userId

    SharedThreadSafeUnorderedMap<int64, GatewayUserPtr> m_safeUsers;         // key=userId

    SharedThreadSafeUnorderedMap<int64, PrevGameServerEntry> m_safePrevGameServer;   // key=userId

    static constexpr int64 k_prevGameServerTtlMs = 5 * 60 * 1000;   // 5분

    // 게임서버 세션 (key=gameServerId). 게임서버 핸드셰이크 후 등록
    SharedThreadSafeUnorderedMap<int32, netlib::ISessionPtr> m_safeGameServerSessions;

    // 게임서버 정보 캐시 (레지스트리 폴링으로 갱신)
    SharedThreadSafeUnorderedMap<int32, ServerInfo>    m_safeGameServerInfos;   // key=gameServerId

    // 패킷 디스패처
    serverbase::PacketDispatcher m_gameServerDispatcher;
    serverbase::PacketDispatcher m_loginServerDispatcher;

    // 네트워크 이벤트 핸들러
    netlib::FuncEventHandler m_clientListenEventHandler;
    netlib::FuncEventHandler m_internalListenEventHandler;
};
