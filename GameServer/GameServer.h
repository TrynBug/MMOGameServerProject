#pragma once

#include "pch.h"
#include "GameServerDefine.h"
#include "OpenField.h"
#include "User.h"
#include "ThreadSafeUnorderedMap.h"


// GameServer는 게임로직(Stage, 유저, 전투, 스킬 등)을 처리하는 서버이다.
// - 클라이언트와 직접 연결되지 않는다. 게이트웨이서버를 통해 클라이언트와 통신한다.
// - 모든 게이트웨이서버에 connect 한다. connect 직후 GameServerHandshakeNtf를 전송하여 자신을 식별시킨다.
// - 채팅서버로부터의 연결을 받는다. (InternalListener)
// - 레지스트리서버로부터 게이트웨이 정보를 폴링한다.
// - GameDB에 캐릭터 데이터를 protobuf JSON으로 저장한다.
class GameServer : public serverbase::ServerBase
{
public:
    GameServer() = default;
    ~GameServer() override = default;

    GameServer(const GameServer&) = delete;
    GameServer& operator=(const GameServer&) = delete;

protected:
    // ServerBase 훅
    bool OnInitialize()                              override;
    void OnServerInfoUpdated(const ServerInfo& info) override;
    void OnBeforeShutdown()                          override;
    void OnShutdown()                                override;

    // InternalListener: 채팅서버 등 다른 서버가 게임서버로 connect 할 때 사용
    netlib::FuncEventHandler* GetInternalListenEventHandler() override { return &m_internalListenEventHandler; }

private:
    // ── 내부 서버 네트워크 이벤트 핸들러 (채팅서버 등이 게임서버로 connect) ─────
    bool onInternalAccept(const netlib::ISessionPtr& spSession);
    void onInternalDisconnect(const netlib::ISessionPtr& spSession);

    // ── 게이트웨이서버 네트워크 이벤트 핸들러 (게임서버 → 게이트웨이서버 connect) ───
    void onGatewayConnect(const netlib::ISessionPtr& spSession);
    void onGatewayDisconnect(const netlib::ISessionPtr& spSession);

    // ── 게이트웨이서버 연결 관리 ───────────────────────────────────
    void connectToGateway(int32 gatewayId, const std::string& ip, uint16 port);
    void disconnectFromGateway(int32 gatewayId);

    // 게이트웨이로 GameServerHandshakeNtf 전송
    void sendGameServerHandshake(const netlib::ISessionPtr& spGatewaySession);

    // 세션에서 GatewaySessionMetaInfo를 꺼낸다.
    static GatewaySessionMetaInfo* getGatewaySessionMeta(const netlib::ISessionPtr& spSession);

    // ── 게이트웨이로부터 받은 유저 관련 패킷 핸들러 ────────────────
    // PacketDispatcher::Register<T>가 자동 역직렬화 후 호출하므로 메시지 객체로 받는다.
    // handleGatewayUserEnter는 DB 조회를 위해 코루틴으로 작성한다.
    db::DBTask<void> handleGatewayUserEnter(netlib::ISessionPtr spSession, ServerPacket::GatewayUserEnterNtf msg);
    void handleGatewayUserDisconnect(const netlib::ISessionPtr& spSession, const ServerPacket::GatewayUserDisconnectNtf& msg);

    // 게이트웨이로부터 받은 클라 패킷 (사이드카 있음) 처리.
    // 사이드카에서 userId 추출 후 해당 유저의 패킷 큐에 push.
    void handleRelayedClientPacket(const netlib::PacketPtr& spPacket);

    // ── 게임서버 → 클라이언트 패킷 전송 helper ─────────────────────
    // 게이트웨이를 통해 GameToGatewayPacketNtf로 래핑하여 단일 유저에게 전송.
    template <typename TMessage>
    void sendPacketToUser(int64 userId, int32 packetType, const TMessage& message);

    // 유저 입장 완료 알림 (GameEnterNtf) 전송
    void sendGameEnterNtf(int64 userId, const DataStructures::Character& character);

    // ── GameDB ────────────────────────────────────────────────────
    // GameDB 스키마 초기화 (서버 시작 시 1회, CREATE TABLE IF NOT EXISTS).
    // 샘플 데이터는 적용하지 않는다 (init_gamedb.bat에서 수동 처리).
    void initGameDB();

private:
    // 오픈필드 (게임서버당 1개, 컨텐츠 스레드 0번에 배정)
    OpenFieldPtr m_spOpenField;

    // ── 내부 서버용 이벤트 핸들러, 패킷 디스패처 ───────────────────
    netlib::FuncEventHandler     m_internalListenEventHandler;
    serverbase::PacketDispatcher m_internalPacketDispatcher;

    // ── 게이트웨이서버 연결 관리 (LoginServer 패턴) ───────────────
    SharedThreadSafeUnorderedMap<int32, netlib::ISessionPtr> m_safeGatewaySessions;
    ExclusiveThreadSafeUnorderedMap<int32, netlib::NetClientPtr> m_safeGatewayClients;
    SharedThreadSafeUnorderedMap<int32, ServerInfo> m_safeGatewayInfos;

    netlib::FuncEventHandler     m_gatewayEventHandler;
    serverbase::PacketDispatcher m_gatewayDispatcher;

    // ── 글로벌 유저 맵 (key=userId, value=UserPtr) ────────────────
    // IOCP Worker가 게이트웨이로부터 패킷을 받았을 때 어떤 유저인지 찾는 용도.
    // Stage가 유저를 소유하므로 여기에 들어있는 shared_ptr는 유저 lifetime의
    // 또 다른 소유자가 된다. Stage에서 제거되어도 여기서 제거되어야 객체가 사라진다.
    SharedThreadSafeUnorderedMap<int64, UserPtr> m_safeUsers;

    // ── GameDB ────────────────────────────────────────────────────
    // 코루틴으로 co_await ExecuteAsync() 사용.
    db::AsyncDBQueue m_dbQueue;
};


// ─────────────────────────────────────────────────────────────
// template 구현
// ─────────────────────────────────────────────────────────────
template <typename TMessage>
void GameServer::sendPacketToUser(int64 userId, int32 packetType, const TMessage& message)
{
    UserPtr spUser;
    if (!m_safeUsers.Find(userId, spUser) || !spUser)
    {
        LOG_WRITE(LogLevel::Warn, std::format("sendPacketToUser - user not found. userId={} packetType={}",
            userId, packetType));
        return;
    }

    netlib::ISessionPtr spGatewaySession;
    if (!m_safeGatewaySessions.Find(spUser->GetGatewayId(), spGatewaySession) || !spGatewaySession)
    {
        LOG_WRITE(LogLevel::Warn, std::format("sendPacketToUser - gateway session not found. userId={} gatewayId={} packetType={}",
            userId, spUser->GetGatewayId(), packetType));
        return;
    }

    // 내부 패킷 바디(클라용)를 먼저 직렬화한다.
    std::string payload;
    if (!message.SerializeToString(&payload))
    {
        LOG_WRITE(LogLevel::Error, std::format("sendPacketToUser - failed to serialize payload. userId={} packetType={}",
            userId, packetType));
        return;
    }

    // GameToGatewayPacketNtf로 감싸서 게이트웨이로 전송.
    ServerPacket::GameToGatewayPacketNtf ntf;
    ntf.set_user_id(userId);
    ntf.set_packet_type(packetType);
    ntf.set_payload(std::move(payload));

    auto spPacket = SerializePacket(Common::SERVER_PACKET_ID_GAME_TO_GATEWAY_PACKET_NTF, ntf);
    if (!spPacket)
    {
        LOG_WRITE(LogLevel::Error, std::format("sendPacketToUser - failed to serialize GameToGatewayPacketNtf. userId={} packetType={}",
            userId, packetType));
        return;
    }

    spGatewaySession->Send(spPacket);
}
