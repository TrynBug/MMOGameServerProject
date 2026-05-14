#pragma once

#include "pch.h"
#include "ThreadSafeUnorderedMap.h"


// 로그인서버는 클라이언트의 최초 접속 및 로그인을 처리한다. 
// 중복 로그인일 경우 현재 유저가 접속중인 게이트웨이서버에 중복 로그인을 알린다.
// 로그인 성공 시 인증토큰을 생성하고 접속할 게이트웨이서버를 선택한다. 5분이내에 재접속한거면 이전에 접속했던 서버를 선택하고, 아니라면 유저가 적은 서버를 선택한다. 
// 그런다음 게이트웨이서버에 인증토큰을 전달하고, 클라에게는 선택된 게이트웨이 서버 정보와 인증토큰을 전달한다.
class LoginServer : public serverbase::ServerBase
{
public:
    LoginServer() = default;
    ~LoginServer() override = default;

    LoginServer(const LoginServer&) = delete;
    LoginServer& operator=(const LoginServer&) = delete;

protected:
    // ServerBase 훅
    bool OnInitialize()                              override;
    void OnServerInfoUpdated(const ServerInfo& info) override;
    void OnBeforeShutdown()                          override;
    netlib::FuncEventHandler* GetClientListenEventHandler() override { return &m_listenEventHandler; }

private:
    // ── 클라이언트 네트워크 이벤트 핸들러 ─────────────────────────
    bool onClientAccept(const netlib::ISessionPtr& spSession);
    void onClientDisconnect(const netlib::ISessionPtr& spSession);

    // ── 게이트웨이서버 네트워크 이벤트 핸들러 ──────────────────────
    void onGatewayConnect(const netlib::ISessionPtr& spSession);
    void onGatewayDisconnect(const netlib::ISessionPtr& spSession);

    // ── 클라이언트 패킷 핸들러 ─────────────────────────────────
    db::DBTask<void> handleLoginReq(netlib::ISessionPtr spSession, GamePacket::LoginReq msg);

    // ── 게이트웨이서버 패킷 핸들러 ─────────────────────────────────
    void handleGatewayHandshake(const netlib::ISessionPtr& spSession, const ServerPacket::GatewayHandshakeNtf& msg);
    void handleUserDisconnectNtf(const netlib::ISessionPtr& spSession, const ServerPacket::UserDisconnectNtf& msg);

    // 로그인 응답 전송
    void sendLoginSuccess(const netlib::ISessionPtr& spSession, int64 userId, uint64 authToken, const ServerInfo& gatewayInfo);
    void sendLoginFailed(const netlib::ISessionPtr& spSession, const std::string& errorMsg);

    // 게이트웨이 서버 관리
    void connectToGateway(int32 gatewayId, const std::string& ip, uint16 port);
    void disconnectFromGateway(int32 gatewayId);

    // 게이트웨이서버 선택(로드밸런싱): 유저 수가 가장 적고 Running 상태인 게이트웨이 선택, 이전 접속 게이트웨이가 있으면 우선 선택
    std::optional<ServerInfo> selectGateway(int64 userId) const;

    // 선택된 게이트웨이에 인증 토큰 전달
    void sendAuthTokenToGateway(int32 gatewayId, int64 userId, uint64 authToken, int64 expireTimeMs);

    // 이미 로그인 중인 게이트웨이에 중복 로그인 알림
    void sendDuplicateLoginToGateway(int32 gatewayId, int64 userId);

    // 로그인한 유저 정보
    struct LoginEntry
    {
        int64 userId = 0;
        int32 gatewayServerId = 0;  // 현재 접속 중인 게이트웨이 서버 ID
        // loginTime 없음 — loginMap은 유저 끊김 이벤트로만 제거되고 TTL로 만료되지 않음
    };

    // 이전 게이트웨이 접속 정보 (5분 TTL, 빠른 재접속 시 같은 게이트웨이로 유도)
    struct PrevGatewayEntry
    {
        int32 gatewayServerId = 0;
        std::chrono::steady_clock::time_point expireTime;
    };

    void upsertLoginEntry(int64 userId, int32 gatewayServerId);
    void removeLoginEntry(int64 userId);
    std::optional<LoginEntry> findLoginEntry(int64 userId) const;

    void cleanupExpiredPrevGateway(); // prevGatewayMap TTL 만료 항목 정리
    void initAccountDB();             // AccountDB 스키마 초기화 (시작 시 1회)

    // 인증 토큰 생성
    uint64 generateAuthToken();

private:
    SharedThreadSafeUnorderedMap<int64, LoginEntry>       m_safeLoginMap;       // key=userId

    SharedThreadSafeUnorderedMap<int64, PrevGatewayEntry>  m_safePrevGatewayMap; // key=userId

    static constexpr int64 k_authTokenTtlMs  = 5 * 60 * 1000;   // 인증토큰 유효시간 5분
    static constexpr int64 k_prevGatewayTtlMs = 5 * 60 * 1000;   // 이전 게이트웨이 캐시 유효시간 5분

    
    // 게이트웨이서버 세션 
    SharedThreadSafeUnorderedMap<int32, netlib::ISessionPtr> m_safeGatewaySessions; // key=gatewayServerId

    // 게이트웨이서버 sessionId -> gatewayServerId 인덱스
    SharedThreadSafeUnorderedMap<int64, int32>  m_safeSessionToGatewayId;  // key=gateway sessionId

    // 게이트웨이서버 NetClient
    ExclusiveThreadSafeUnorderedMap<int32, netlib::NetClientPtr> m_safeGatewayClients;  // key=gatewayServerId

    // 게이트웨이 서버 정보 캐시 (레지스트리서버 폴링으로 갱신)
    SharedThreadSafeUnorderedMap<int32, ServerInfo>  m_safeGatewayInfos;  //  key=gatewayServerId

    // 랜덤값 생성
    mutable std::mutex  m_rngMutex;
    std::mt19937_64     m_rng { std::random_device{}() };

    // AccountDB
    db::AsyncDBQueue m_dbQueue;

    // 네트워크 이벤트 핸들러, 패킷 디스패처
    netlib::FuncEventHandler     m_listenEventHandler;   // 네트워크 이벤트 핸들러
    serverbase::PacketDispatcher m_packetDispatcher;     // 클라이언트 패킷 디스패처

    netlib::FuncEventHandler     m_gatewayEventHandler;   // 게이트웨이 네트워크 이벤트 핸들러
    serverbase::PacketDispatcher m_gatewayDispatcher;     // 게이트웨이 패킷 디스패처
};
