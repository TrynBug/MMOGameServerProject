#pragma once

#include "pch.h"
#include "GameServerDefine.h"
#include "OpenField.h"
#include "ThreadSafeUnorderedMap.h"


// GameServer는 게임로직(Stage, 유저, 전투, 스킬 등)을 처리하는 서버이다.
// - 클라이언트와 직접 연결되지 않는다. 게이트웨이서버를 통해 클라이언트와 통신한다.
// - 모든 게이트웨이서버에 connect 한다. connect 직후 GameServerHandshakeNtf를 전송하여 자신을 식별시킨다.
// - 채팅서버로부터의 연결을 받는다. (InternalListener)
// - 레지스트리서버로부터 게이트웨이 정보를 폴링한다.
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

private:
    // 오픈필드 (게임서버당 1개, 컨텐츠 스레드 0번에 배정)
    OpenFieldPtr m_spOpenField;

    // ── 내부 서버용 이벤트 핸들러, 패킷 디스패처 ───────────────────
    // 패킷 핸들러 등록은 다음 단계에서 추가 (채팅서버 패킷 등)
    netlib::FuncEventHandler     m_internalListenEventHandler;
    serverbase::PacketDispatcher m_internalPacketDispatcher;

    // ── 게이트웨이서버 연결 관리 ───────────────────────────────────
    // 패턴은 LoginServer와 동일

    // 게이트웨이서버 세션 (핸드셰이크 완료 후 등록)
    SharedThreadSafeUnorderedMap<int32, netlib::ISessionPtr> m_safeGatewaySessions;

    // 게이트웨이서버 NetClient (connect 시 등록, disconnect 시 제거)
    ExclusiveThreadSafeUnorderedMap<int32, netlib::NetClientPtr> m_safeGatewayClients;

    // 게이트웨이서버 정보 캐시 (레지스트리 폴링으로 갱신)
    SharedThreadSafeUnorderedMap<int32, ServerInfo> m_safeGatewayInfos;

    // 게이트웨이서버 이벤트 핸들러, 패킷 디스패처
    // 게이트웨이가 게임서버로 보내는 패킷(UserEnterNtf 등) 핸들러 등록은 다음 단계에서 추가
    netlib::FuncEventHandler     m_gatewayEventHandler;
    serverbase::PacketDispatcher m_gatewayDispatcher;
};
