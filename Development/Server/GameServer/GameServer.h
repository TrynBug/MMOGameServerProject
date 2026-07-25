#pragma once

#include "pch.h"
#include <cassert>
#include "GameServerDefine.h"
#include "Stages/SystemStage.h"
#include "Stages/Town.h"
#include "User.h"
#include "Managers/StageManager.h"
#include "Managers/NavMeshManager.h"
#include "Managers/StageAssetManager.h"
#include "ThreadSafeUnorderedMap.h"
#include "PacketSender.h"
#include "Managers/CheatManager.h"
#include "Managers/CastAnchorRegistry.h"
#include "Managers/ChatManager.h"

// GameServer는 게임로직(Stage, 유저, 전투, 스킬 등)을 처리하는 서버이다.
// - 클라이언트와 직접 연결되지 않는다. 게이트웨이서버를 통해 클라이언트와 통신한다.
// - 모든 게이트웨이서버에 connect 한다. connect 직후 GameServerHandshakeNtf를 전송하여 자신을 식별시킨다.
// - CommunicationServer로부터의 연결을 받는다. (InternalListener)
// - 레지스트리서버로부터 게이트웨이와 CommunicationServer 정보를 폴링한다.
// - GameDB에 캐릭터 데이터를 protobuf JSON으로 저장한다.
class GameServer : public serverbase::ServerBase
{
public:
    GameServer()
        : m_packetSender(*this, m_safeUsers, m_safeGatewaySessions)
        , m_chatManager(*this, m_safeUsers, m_safeCommunicationSessions, m_packetSender)
    {
        assert(s_pInstance == nullptr);   // 인스턴스는 반드시 1개
        s_pInstance = this;
    }
    ~GameServer() override { s_pInstance = nullptr; }

    GameServer(const GameServer&) = delete;
    GameServer& operator=(const GameServer&) = delete;

    // ── 전역 단일 인스턴스 접근 ────────────────────────────────
    // 비용 = 포인터 1회 로드(인라인). lazy-init 가드가 없어 Meyers 싱글톤보다 싸다.
    // s_pInstance 는 main 에서 GameServer 가 1회 생성될 때(단일 스레드 시작 구간) 세팅되고
    // 이후로는 읽기만 하므로 동기화가 필요 없다.
    static GameServer& Instance() { return *s_pInstance; }

    // 생성 전 / 소멸 후에는 nullptr 일 수 있는 컨텍스트(예: 콘솔 컨트롤 핸들러)에서 안전하게 접근.
    static GameServer* InstanceOrNull() { return s_pInstance; }

    // ── 패킷 송신 ──────────────────────────────────────────────
    // 클라이언트로 나가는 모든 Send***Ntf 는 PacketSender 가 담당한다. Stage/컴포넌트는
    // GetPacketSender() 로 얻어서 호출한다 (예: GetPacketSender().SendSnapshotNtf(...)).
    PacketSender&       GetPacketSender()       { return m_packetSender; }
    const PacketSender& GetPacketSender() const { return m_packetSender; }

    // ── 매니저 접근 ──────────────────────────────────────────────
    NavMeshManager&       GetNavMeshManager()       { return m_navMeshManager; }
    const NavMeshManager& GetNavMeshManager() const { return m_navMeshManager; }

    StageAssetManager&       GetStageAssetManager()       { return m_stageAssetManager; }
    const StageAssetManager& GetStageAssetManager() const { return m_stageAssetManager; }

    StageManager&       GetStageManager()       { return m_stageManager; }
    const StageManager& GetStageManager() const { return m_stageManager; }

    CheatManager&       GetCheatManager()       { return m_cheatManager; }
    const CheatManager& GetCheatManager() const { return m_cheatManager; }

    CastAnchorRegistry&       GetCastAnchorRegistry()       { return m_castAnchorRegistry; }
    const CastAnchorRegistry& GetCastAnchorRegistry() const { return m_castAnchorRegistry; }

    ChatManager&       GetChatManager()       { return m_chatManager; }
    const ChatManager& GetChatManager() const { return m_chatManager; }

    // 특정 게이트웨이로 서버패킷 전송. 해당 게이트웨이 세션이 없으면 false. (netdelay 치트 등 내부 제어용)
    bool SendToGateway(int32 gatewayId, const netlib::PacketPtr& spPacket);

    // 크로스서버 이동 개시 (출발 서버). Stage::handleStageMoveReq 의 크로스서버 분기가 호출한다.
    //   1) 현재 캐릭터를 DB에 저장(UPDATE)
    //   2) (성공 시) 게이트웨이에 UserMoveToGameServerReq 전송 (게이트웨이가 목적지 서버로 재라우팅)
    //   3) (성공 시) 출발 Stage에서 퇴장(UserLeave 메시지) + 글로벌 유저맵에서 제거
    // 호출 시점에 유저는 아직 출발 Stage에 남아있다("유저는 반드시 Stage에 속한다" 불변식 유지). 퇴장은
    //   저장+게이트웨이 통보가 모두 성공한 뒤에만 일어난다. 실패 시 유저가 떠난 적 없어 InStage 복귀로 롤백된다(무소속 유저 없음).
    // pSourceStage: 출발 Stage. DB await 후속작업이 이 Stage의 컨텐츠 스레드에서 재개되고, 퇴장도 이 Stage로 enqueue된다.
    db::DetachedCoTask BeginCrossServerMove(int64 accountId, int32 targetGameServerId, int32 targetStageDataKey, int32 positionType,
                                            Stage* pSourceStage);

    // 캐릭터 목록을 DB에서 로드해 CharacterListNtf 를 전송한다(DB 코루틴).
    // SystemStage::OnUserEnter 가 호출 — 로그인 최초 입장 / 캐릭터선택 복귀 공통 전송 경로(전송 시점 일원화).
    // pResumeExecutor: DB await 후속작업(전송)을 재개할 executor. SystemStage 의 resume executor 를 넘겨 SystemStage 의 컨텐츠 스레드에서 재개되게 한다.
    db::DetachedCoTask SendCharacterListForUser(int64 accountId, int32 gameDbIndex, db::IResumeExecutor* pResumeExecutor);

    // [개발/테스트] Stage에서 시작하는 코루틴 절차 검증용 (치트 savechar 가 호출).
    // AsyncPin 획득 → saveCharacterToDB(캐릭터를 DB에 UPDATE)를 Stage의 resume executor로 co_await
    //   → 후속작업이 같은 Stage 스레드에서 재개되는지 + 핀 카운터 증감을 로그([savechar])로 확인한다.
    db::DetachedCoTask SaveCharacterFromStage(Stage* pStage, CharacterPtr spChar);

    // [개발/테스트] Currency/Item 테이블 upsert 검증용. Stage::handleEventAreaEnterReq 가 호출한다.
    // AsyncPin 획득 → 임의 데이터로 Currency(캐릭터당 1행) + Item(스노우플레이크 1행)을 한 트랜잭션으로 upsert.
    db::DetachedCoTask UpsertTestCurrencyAndItemFromStage(Stage* pStage, CharacterPtr spChar);

protected:
    // ServerBase 훅
    bool OnInitialize()                              override;
    void OnMetricsCollect()                          override;
    void OnServerInfoUpdated(const ServerInfo& info) override;
    void OnBeforeShutdown()                          override;
    void OnShutdown()                                override;

    // InternalListener: CommunicationServer가 게임서버로 connect 할 때 사용
    netlib::FuncEventHandler* GetInternalListenEventHandler() override { return &m_internalListenEventHandler; }

private:
    // 지정 Stage 의 채널을 GameData_Stage::ChannelCount 만큼 생성한다 (OnInitialize 전용).
    // Town/Field 만 지원 — SystemStage 는 채널 개념이 없고(항상 1개), Dungeon 은 미구현.
    bool createStageChannels(int32 stageDataKey);

    // ── 내부 서버 네트워크 이벤트 핸들러 (CommunicationServer가 게임서버로 connect) ─────
    bool onInternalAccept(const netlib::ISessionPtr& spSession);
    void handleInternalPacket(const netlib::ISessionPtr& spSession, const netlib::PacketPtr& spPacket);
    void onInternalDisconnect(const netlib::ISessionPtr& spSession);
    void handleCommunicationHandshakeReq(const netlib::ISessionPtr& spSession, const ServerPacket::ServerHandshakeReq& msg);

    // ── 게이트웨이서버 네트워크 이벤트 핸들러 (게임서버 -> 게이트웨이서버 connect) ───
    void onGatewayConnect(const netlib::ISessionPtr& spSession);
    void onGatewayDisconnect(const netlib::ISessionPtr& spSession);

    // ── 게이트웨이서버 연결 관리 ───────────────────────────────────
    void connectToGateway(int32 gatewayId, const std::string& ip, uint16 port);
    void disconnectFromGateway(int32 gatewayId);

    // 게이트웨이로 ServerHandshakeReq 전송
    void sendGameServerHandshakeReq(const netlib::ISessionPtr& spGatewaySession);

    // 세션에서 InternalSessionMeta를 꺼낸다.
    static InternalSessionMeta* getInternalSessionMeta(const netlib::ISessionPtr& spSession);

    // ── 게이트웨이로부터 받은 유저 관련 패킷 핸들러 ────────────────
    // PacketDispatcher::Register<T>가 자동 역직렬화 후 호출하므로 메시지 객체로 받는다.
    // handleGatewayUserEnter는 DB 조회를 위해 코루틴으로 작성한다.
    db::DetachedCoTask handleGatewayUserEnter(netlib::ISessionPtr spSession, ServerPacket::GatewayUserEnterNtf msg);
    void handleGatewayUserDisconnect(const netlib::ISessionPtr& spSession, const ServerPacket::GatewayUserDisconnectNtf& msg);

    // 크로스서버 이동 수신 (목적지 서버). 게이트웨이가 보낸 GatewayUserRerouteNtf 처리.
    //   1) DB에서 캐릭터 로드 → User/Character 생성 (캐릭터 선택과 동일 골격)
    //   2) 대상 Stage에 유저 입장(Moving) + 클라에 StageMoveRes(성공) 송신
    //      → 이후 클라의 StageLoadCompleteReq 를 대상 Stage가 받아 스폰한다.
    db::DetachedCoTask handleGatewayUserReroute(netlib::ISessionPtr spSession, ServerPacket::GatewayUserRerouteNtf msg);
    void handleGatewayHandshakeRes(const netlib::ISessionPtr& spSession, const ServerPacket::ServerHandshakeRes& msg);

    // 게이트웨이로부터 받은 클라 패킷 (사이드카 있음) 처리.
    // 사이드카에서 accountId 추출 후 해당 유저의 패킷 큐에 push.
    void handleRelayedClientPacket(const netlib::PacketPtr& spPacket);

    // 유저 입장 완료 알림 (GameEnterNtf) 전송. 현재 SystemStage 입장 단계 완료 시 전송.
    void sendGameEnterNtf(int64 accountId);

    // 캐릭터 목록 전송 (CharacterListNtf). 게임 입장 직후 자동 전송.
    void sendCharacterListNtf(int64 accountId, const std::vector<DataStructures::Character>& characters);

    // 캐릭터 생성 요청 핸들러. DB INSERT 코루틴.
    db::DetachedCoTask handleClientCharacterCreate(int64 accountId, GamePacket::CharacterCreateReq req);

    // 캐릭터 생성 결과 전송 (CharacterCreateRes).
    // 성공 시 pNewCharacter에 생성된 캐릭터, 실패 시 nullptr.
    void sendCharacterCreateRes(int64 accountId, EResultCode resultCode, const std::string& errorMsg, const DataStructures::Character* pNewCharacter);

    // 캐릭터 선택 요청 핸들러. DB SELECT 이후 SystemStage → Town 이동.
    db::DetachedCoTask handleClientCharacterSelect(int64 accountId, GamePacket::CharacterSelectReq req);


    // 캐릭터 선택 결과 전송 (CharacterSelectRes). 성공/실패 모두 이 함수로 송신.
    //   - 성공: resultCode=Success, errorMsg="", pCharacter(전체 데이터)/stageDataKey 채움 (클라는 데이터모델 보관 후 로딩 시작)
    //   - 실패: resultCode=Fail,    errorMsg=사유, pCharacter=nullptr
    // 스폰 좌표는 이 패킷에 싣지 않는다. 로딩 완료 후 StageLoadCompleteRes가 좌표의 단일 출처.
    void sendCharacterSelectRes(int64 accountId, EResultCode resultCode, const std::string& errorMsg,
                                const DataStructures::Character* pCharacter, int32 stageDataKey);

    // (account_id, character_id)로 DB에서 캐릭터 row를 읽어 JSON 파싱 후 Character 객체를 생성하고
    // User에 소유 연결한다(User→Character 강참조, Character→User 약참조).
    db::AwaitableCoTask<CharacterPtr> loadCharacterForUser(int64 accountId, int64 characterId, UserPtr spUser);

    // 계정의 모든 캐릭터를 DB에서 로드한다 (CharacterListNtf 전송용).
    // pResumeExecutor: DB await 후속작업을 재개할 executor (호출 문맥의 스레드 선택).
    db::AwaitableCoTask<std::vector<DataStructures::Character>> loadAllCharactersForUser(int64 accountId, int32 gameDbIndex, db::IResumeExecutor* pResumeExecutor);

    // 캐릭터의 런타임 상태를 proto에 동기화(SyncRuntimeToProto)한 뒤 JSON 직렬화하여 DB에 저장(UPDATE)한다.
    // pResumeExecutor: DB await 후속작업을 재개할 executor(호출 문맥의 스레드 선택). Stage에서 호출 시 그 Stage의 executor.
    db::AwaitableCoTask<bool> saveCharacterToDB(CharacterPtr spCharacter, db::IResumeExecutor* pResumeExecutor);

    // AccountDB에서 계정(DataStructures::Account)을 읽는다. 실패/미발견 시 nullopt.
    db::AwaitableCoTask<std::optional<DataStructures::Account>> loadAccount(int64 accountId);

private:
    // 전역 단일 인스턴스 포인터. 생성자에서 1회 세팅, 소멸자에서 해제. Instance() 가 역참조한다.
    static inline GameServer* s_pInstance = nullptr;

    // NavMesh 데이터 관리, 길찾기 기능 제공
    NavMeshManager m_navMeshManager;
    StageAssetManager m_stageAssetManager;

    // 모든 Stage(SystemStage / Town / Field / Dungeon)를 관리한다.
    // 생성, 조회, 컨텐츠 스레드 배정, GameServer 주입을 캡슐화.
    StageManager m_stageManager;

    // 치트 처리 + 치트 상태(플래그 등) 보관. 개발용.
    // (CHEAT_REQ 패킷 수신은 _DEBUG 한정이지만, 플래그 조회를 위해 클래스는 항상 존재한다.)
    CheatManager m_cheatManager;

    // Caster의 스킬 시전 위치 Anchor를 보관해두는 컴포넌트.
    CastAnchorRegistry m_castAnchorRegistry;

    // ── 내부 서버용 이벤트 핸들러, 패킷 디스패처 ───────────────────
    netlib::FuncEventHandler     m_internalListenEventHandler;
    serverbase::PacketDispatcher m_internalPacketDispatcher;

    // CommunicationServer가 connect하는 방향이다. handshake 완료 뒤 채팅/소셜 패킷 수신에 사용한다.
    SharedThreadSafeUnorderedMap<int32, netlib::ISessionPtr> m_safeCommunicationSessions;

    // Registry에서 확인한 CommunicationServer 목록. incoming handshake의 serverId 검증에 사용한다.
    SharedThreadSafeUnorderedMap<int32, ServerInfo> m_safeCommunicationInfos;

    // ── 게이트웨이서버 연결 관리 ───────────────
    SharedThreadSafeUnorderedMap<int32, netlib::ISessionPtr> m_safeGatewaySessions;
    ExclusiveThreadSafeUnorderedMap<int32, netlib::NetClientPtr> m_safeGatewayClients;
    SharedThreadSafeUnorderedMap<int32, ServerInfo> m_safeGatewayInfos;

    netlib::FuncEventHandler     m_gatewayEventHandler;
    serverbase::PacketDispatcher m_gatewayDispatcher;

    // ── 글로벌 유저 맵 (key=accountId, value=UserPtr) ────────────────
    // IOCP Worker가 게이트웨이로부터 패킷을 받았을 때 어떤 유저인지 찾는 용도.
    // Stage가 유저를 소유하므로 여기에 들어있는 shared_ptr는 유저 lifetime의
    // 또 다른 소유자가 된다. Stage에서 제거되어도 여기서 제거되어야 객체가 사라진다.
    ShardedThreadSafeUnorderedMap<int64, UserPtr> m_safeUsers;

    // ── 패킷 송신 (PacketSender) ─────────────────────────────────
    // 클라이언트로 나가는 Ntf 송신 전담. m_server(=*this) + 위 두 맵(유저/게이트웨이세션)을 참조로 받는다.
    // 위 두 맵보다 뒤에 선언하여 초기화 순서상 안전하게 참조를 바인딩한다.
    PacketSender m_packetSender;

    // GameServer 범위 이상의 채팅, 귓속말, presence와 CommunicationServer 채팅 패킷을 처리한다.
    // 참조하는 유저/CommunicationServer 맵과 PacketSender보다 뒤에 선언한다.
    ChatManager m_chatManager;
};
