#pragma once

#include "pch.h"
#include "User.h"
#include "Sector.h"
#include "StageObject.h"

#include "Enum/GameEnum_Stage.h"   // EStageType

#include <variant>


// GameServer와의 직접 의존을 피하기 위한 forward declaration.
// (Stage 파생 클래스에서 GameServer의 서비스(패킷 전송 등)를 호출해야 할 때 사용.)
class GameServer;

// Character forward declaration (StageMsg_UserEnter 등에서 사용).
class Character;
using CharacterPtr = std::shared_ptr<Character>;


// StageGridParams: GameData_Stage에서 읽어온 Stage 공간 정보.
// Stage 파생 클래스가 생성자에서 LoadStageGridParams를 1회 호출한 다음
// 그 결과를 Stage 기본 생성자에 펼쳐서 전달한다.
struct StageGridParams
{
    EStageType stageType  = EStageType::None;
    double     worldMinX  = 0.0;
    double     worldMinY  = 0.0;
    double     worldMaxX  = 0.0;
    double     worldMaxY  = 0.0;
    double     sectorSize = 0.0;
};

// stageId로 GameData_Stage를 조회하여 StageGridParams를 채워 리턴한다.
// 데이터가 없으면 에러 로그를 남기고 기본값(fallback)을 리턴.
StageGridParams LoadStageGridParams(int64 stageId);

// ─────────────────────────────────────────────────────────────
// Stage 시스템 메시지
// 외부 스레드(IOCP Worker)에서 Stage에 비동기로 알림을 전달할 때 사용.
// std::variant로 표현하여 핸들러 분기를 깔끔하게 처리한다.
// ─────────────────────────────────────────────────────────────

// 유저 입장.
// spCharacter가 있으면 Character도 함께 Stage 객체 컨테이너(m_objects, m_userObjects)에 등록된다.
// SystemStage 입장 시에는 spCharacter == nullptr (캐릭터 선택 전).
// Town/Field/Dungeon 입장 시에는 spCharacter가 설정되어야 한다.
struct StageMsg_UserEnter
{
    UserPtr      spUser;
    CharacterPtr spCharacter;   // 선택된 캐릭터 객체. 없으면 nullptr.
};

// 유저 퇴장 (게이트웨이로부터 GatewayUserDisconnectNtf 수신 시,
// 또는 다른 Stage로 이동할 때)
struct StageMsg_UserLeave
{
    int64 userId = 0;
};

// 모든 메시지 타입의 variant
using StageMessage = std::variant<
    StageMsg_UserEnter,
    StageMsg_UserLeave
>;


// ─────────────────────────────────────────────────────────────
// Stage 베이스 클래스
// ─────────────────────────────────────────────────────────────
//
// 캐릭터가 돌아다닐 수 있는 1개의 필드를 나타낸다.
// 단일 컨텐츠 스레드에서만 업데이트되므로 내부 로직은 단일 스레드로 작성한다.
// 외부에서 Stage에 영향을 주려면 EnqueueMessage()로 시스템 메시지를 보낸다.
// 자세한 설계는 서버구조개요.md의 'Stage 클래스' 절 참조.
//
// ── Stage 종류 (EStageType) ──
// Stage 종류는 GameData/GameEnum으로 관리되는 EStageType (System/Town/Field/Dungeon)을 사용한다.
//
// ── 공간 분할 (섹터) ──
// 맵 전체 영역(worldMin/Max) 을 sectorSize 크기의 격자로 분할한다.
// 섹터는 AOI 계산, 브로드캐스팅 범위 제한 등에 사용된다.
// 모든 섹터를 미리 생성한다 (lazy creation 안 함).
//
// 맵 영역 / sectorSize 의 자료형:
//   - GameData_Stage에서 double로 제공되므로 Stage도 그대로 double로 보관한다.
//   - 런타임 객체(StageObject)의 좌표는 float이다. 좌표 → 섹터 인덱스 변환 시
//     섞이는데, 명시적 캐스팅으로 처리한다.
//
// ── 객체 소유 ──
// Stage가 모든 StageObject (유저/몬스터/프랍/드롭)를 shared_ptr로 소유한다.
// 빠른 타입별 조회를 위해 통합 맵 + 타입별 맵을 함께 유지한다.
//
// 향후 단계에서 추가될 것:
//   - User → StageObject 상속
//   - 객체 입장/퇴장 시 섹터 갱신
//   - AOI 진입/이탈 판정 및 패킷 전송
//   - NavMesh 연결
//   - 몬스터 / NPC
class Stage : public serverbase::Contents
{
public:
    // 명시적 grid 값으로 생성.
    Stage(int64 stageId, EStageType stageType,
          double worldMinX, double worldMinY,
          double worldMaxX, double worldMaxY,
          double sectorSize);

    ~Stage() override = default;

    Stage(const Stage&) = delete;
    Stage& operator=(const Stage&) = delete;

public:
    int64      GetStageId()   const { return m_stageId; }
    EStageType GetStageType() const { return m_stageType; }

    // ── 맵/섹터 정보 조회 ──
    double GetWorldMinX()    const { return m_worldMinX; }
    double GetWorldMinY()    const { return m_worldMinY; }
    double GetWorldMaxX()    const { return m_worldMaxX; }
    double GetWorldMaxY()    const { return m_worldMaxY; }
    double GetSectorSize()   const { return m_sectorSize; }
    int32  GetSectorCountX() const { return m_sectorCountX; }
    int32  GetSectorCountY() const { return m_sectorCountY; }

    // 좌표 (posX, posY) -> 섹터 인덱스 (sectorX, sectorY) 변환.
    // 맵 영역 바깥이면 false 리턴.
    // posX/Y는 런타임 객체 좌표계(float). 내부 계산은 double로 처리.
    bool GetSectorIndex(float posX, float posY, int32& outSectorX, int32& outSectorY) const;

    // 섹터 좌표가 유효한지 확인.
    bool IsValidSectorIndex(int32 sectorX, int32 sectorY) const;

    // 섹터 조회 (좌표 유효성 검사 후 포인터 반환). 유효하지 않으면 nullptr.
    Sector*       GetSector(int32 sectorX, int32 sectorY);
    const Sector* GetSector(int32 sectorX, int32 sectorY) const;

    // 좌표로 섹터 직접 조회. 맵 영역 바깥이면 nullptr.
    Sector*       GetSectorByPos(float posX, float posY);

    // GameServer 주입. 생성 직후 소유자가 설정한다.
    // Stage 파생 클래스가 패킷 전송 등을 위해 사용.
    void          SetGameServer(GameServer* pGameServer) { m_pGameServer = pGameServer; }
    GameServer*   GetGameServer() const                  { return m_pGameServer; }

    // 외부 스레드에서 시스템 메시지를 push (thread-safe).
    // 다음 OnUpdate에서 처리된다.
    void      EnqueueMessage(StageMessage msg);

protected:
    // serverbase::Contents 훅
    void OnStart()              override;
    void OnUpdate(int64 deltaMs) override;
    void OnStop()               override;

    // Stage 파생 클래스의 매 tick 로직.
    // OnUpdate 안에서 시스템 메시지 처리 이후에 호출된다.
    virtual void OnStageUpdate(int64 deltaMs) {}

    // ── 시스템 메시지 처리 hooks (파생 클래스가 override 가능) ──
    // 기본 동작: 유저 추가/제거 및 로그 출력.
    // spCharacter가 nullptr이 아니면 Character를 m_objects/m_userObjects에도 등록.
    virtual void OnUserEnter(const UserPtr& spUser, const CharacterPtr& spCharacter);
    virtual void OnUserLeave(int64 userId);

    // 유저가 클라이언트로부터 보낸 패킷 처리 hook (파생 클래스가 override 가능).
    // 기본 동작: 로그만 출력. 향후 단계에서 실제 디스패쳐 호출 등을 추가한다.
    virtual void OnUserPacket(const UserPtr& spUser, const netlib::PacketPtr& spPacket);

    // ── 유저 컨테이너 접근 ──
    const std::unordered_map<int64, UserPtr>& GetUsers() const { return m_users; }

private:
    // 시스템 메시지 큐 처리
    void processSystemMessages();

    // 각 유저의 클라 패킷 큐 drain 및 처리
    void processUserPackets();

    // 섹터 그리드 초기화 (생성자에서 1회 호출)
    void initializeSectorGrid();

    // 2D 섹터 좌표 → 1D 배열 인덱스 변환
    int32 sectorIndexToFlat(int32 sectorX, int32 sectorY) const { return sectorY * m_sectorCountX + sectorX; }

private:
    int64      m_stageId   = 0;
    EStageType m_stageType = EStageType::None;

    // GameServer 포인터 (소유권 없음, 주입자가 lifetime 보장).
    // SetGameServer로 주입되며, Stage 파생이 패킷 전송 등을 하기 위해 사용.
    GameServer* m_pGameServer = nullptr;

    // 5초 주기 heartbeat 로그용 누적 시간
    int64      m_heartbeatAccumMs = 0;

    // ── 맵/섹터 정보 ─────────────────────────────────────────────
    // GameData_Stage에서 double로 제공되므로 멤버도 double.
    // (런타임 객체 좌표는 float이지만, 맵 영역/섹터 제원은
    //  게임데이터 계산 일관성을 위해 double로 도입한 값을 그대로 보관.)
    double m_worldMinX = 0.0;
    double m_worldMinY = 0.0;
    double m_worldMaxX = 0.0;
    double m_worldMaxY = 0.0;
    double m_sectorSize = 0.0;
    int32  m_sectorCountX = 0;
    int32  m_sectorCountY = 0;

    // 섹터 그리드 (1D 배열로 저장, sectorIndexToFlat()로 인덱싱).
    // 크기 = m_sectorCountX * m_sectorCountY. 생성자에서 한 번 초기화 후 크기 변경 없음.
    std::vector<Sector> m_sectors;

    // ── 시스템 메시지 큐 (외부 스레드 push, 컨텐츠 스레드 drain) ──
    // swap-and-drain 패턴: 외부에서는 m_pendingMessages에 추가, 컨텐츠 스레드는
    // 락 잠깐 잡고 swap한 뒤 락 풀고 순차 처리. 락 충돌 최소화.
    std::mutex                m_pendingMessagesMutex;
    std::vector<StageMessage> m_pendingMessages;

    // ── 유저 컨테이너 (컨텐츠 스레드 전용 접근) ──
    // Stage가 유저를 소유 (shared_ptr).
    // (3단계에서 User가 StageObject를 상속받으면 m_objects로 옮겨질 예정)
    std::unordered_map<int64, UserPtr> m_users;

    // ── StageObject 통합 + 타입별 소유 컨테이너 (3단계 이후 사용 예정) ──
    // 모든 StageObject의 lifetime은 m_objects가 관리(shared_ptr).
    // 타입별 맵(m_userObjects 등)은 같은 shared_ptr을 빠른 조회용으로 보관.
    // 현재 단계에서는 자료구조만 선언. 실제 사용은 3단계 이후.
    std::unordered_map<int64, StageObjectPtr> m_objects;
    std::unordered_map<int64, StageObjectPtr> m_userObjects;
    std::unordered_map<int64, StageObjectPtr> m_monsterObjects;
    std::unordered_map<int64, StageObjectPtr> m_propObjects;
    std::unordered_map<int64, StageObjectPtr> m_dropObjects;
};

using StagePtr  = std::shared_ptr<Stage>;
using StageWPtr = std::weak_ptr<Stage>;
