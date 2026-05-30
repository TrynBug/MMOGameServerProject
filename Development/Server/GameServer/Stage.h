#pragma once

#include "pch.h"
#include "User.h"
#include "Sector.h"
#include "StageObject.h"

#include "Enum/GameEnum_Stage.h"   // EStageType
#include "Generated/GameData_Stage.h"

#include <variant>

// Detour forward declaration.
// SetNavMesh 가 const dtNavMesh* 를 받기 위해서만 필요. 다른 dtXxx 는 StageNavMesh 가 흡수.
class dtNavMesh;

// NavMesh 길찾기 헬퍼는 StageNavMesh 로 분리되어 있다 (Sector 처럼).
// 헤더에서는 전방선언만, 멤버는 unique_ptr 로 보관 (Stage 소멸자가 .cpp 에 있어야 함).
class StageNavMesh;


// GameServer와의 직접 의존을 피하기 위한 forward declaration.
// (Stage 파생 클래스에서 GameServer의 서비스(패킷 전송 등)를 호출해야 할 때 사용.)
class GameServer;

// Character forward declaration (StageMsg_UserEnter 등에서 사용).
class Character;
using CharacterPtr = std::shared_ptr<Character>;

// Monster forward declaration (SpawnMonster 리턴 타입). 완전타입은 Stage.cpp 에서 include.
class Monster;


// StageGridParams: Stage 공간 정보 + NavMesh 매핑.
// Stage 파생 클래스가 생성자에서 LoadStageGridParams 로 일부 필드를 채우고,
// 그 뒤에 NavMeshManager 의 NavMeshMeta 로 worldMin/Max 를 채워 Stage 기본 생성자로 전달한다.
struct StageGridParams
{
    EStageType  stageType       = EStageType::None;
    std::string navMeshFileName;     // GameData_Stage::NavMeshFileName. NavMeshManager Find 의 키.
    double      worldMinX  = 0.0;
    double      worldMinZ  = 0.0;
    double      worldMaxX  = 0.0;
    double      worldMaxZ  = 0.0;
    double      sectorSize = 0.0;
};

// stageDataKey로 GameData_Stage를 조회하여 StageGridParams의 stageType / navMeshFileName / sectorSize 를 채운다.
// worldMin/Max 는 여기서 채우지 않는다 (NavMesh 메타에서 가져와야 하므로 호출자가 채운다).
// 데이터가 없으면 에러 로그를 남기고 기본값(fallback)을 리턴.
StageGridParams LoadStageGridParams(int64 stageDataKey);

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
    Stage(int64 stageId, int64 stageDataKey, EStageType stageType,
          double worldMinX, double worldMinZ,
          double worldMaxX, double worldMaxZ,
          double sectorSize);

    // 다형성 소멸자 + dtNavMeshQuery / dtQueryFilter 정리 필요해 .cpp 에서 구현.
    ~Stage() override;

    Stage(const Stage&) = delete;
    Stage& operator=(const Stage&) = delete;

public:
    int64      GetStageId()   const { return m_stageId; }
    EStageType GetStageType() const { return m_stageType; }
	int64 	   GetStageDataKey() const { return m_pStageData->Key; }
	const GameData_Stage* pGetStageData() const { return m_pStageData; }

    // ── 맵/섹터 정보 조회 (X-Z 평면) ──
    double GetWorldMinX()    const { return m_worldMinX; }
    double GetWorldMinZ()    const { return m_worldMinZ; }
    double GetWorldMaxX()    const { return m_worldMaxX; }
    double GetWorldMaxZ()    const { return m_worldMaxZ; }
    double GetSectorSize()   const { return m_sectorSize; }
    int32  GetSectorCountX() const { return m_sectorCountX; }
    int32  GetSectorCountZ() const { return m_sectorCountZ; }

    // 좌표 (posX, posZ) -> 섹터 인덱스 (sectorX, sectorZ) 변환.
    // 맵 영역 바깥이면 false 리턴.
    // posX/Z는 런타임 객체 좌표계(float). 내부 계산은 double로 처리.
    bool GetSectorIndex(float posX, float posZ, int32& outSectorX, int32& outSectorZ) const;

    // 섹터 좌표가 유효한지 확인.
    bool IsValidSectorIndex(int32 sectorX, int32 sectorZ) const;

    // 섹터 조회 (좌표 유효성 검사 후 포인터 반환). 유효하지 않으면 nullptr.
    Sector*       GetSector(int32 sectorX, int32 sectorZ);
    const Sector* GetSector(int32 sectorX, int32 sectorZ) const;

    // 좌표로 섹터 직접 조회. 맵 영역 바깥이면 nullptr.
    Sector*       GetSectorByPos(float posX, float posZ);

    // ── NavMesh 길찾기 ───────────────────────────────────
    // 실제 길찾기 로직은 StageNavMesh 에 위임. Stage 는 lifetime 관리만.
    //
    // SetNavMesh: NavMeshManager 로부터 받은 dtNavMesh 로 StageNavMesh 인스턴스 생성.
    //   StageManager 가 Stage 생성 직후 호출한다. nullptr 도 허용 (길찾기 비활성화).
    void SetNavMesh(const dtNavMesh* pNavMesh);

    // 길찾기. StageNavMesh 가 초기화 안 됐거나 nullptr 이면 false.
    // 좌표계: Unity 와 동일. outWaypoints 는 (x,y,z) 세트 순서로 채워짐.
    bool FindPath(float startX, float startY, float startZ,
                  float endX,   float endY,   float endZ,
                  std::vector<float>& outWaypoints) const;

    // NavMesh 가 설정/준비되어 길찾기·스냅이 가능한 상태인지.
    bool HasNavMesh() const;

    // 점을 NavMesh 표면으로 스냅한다 (Y 보정 + walkable 검증). StageNavMesh 에 위임.
    // 검색 박스(halfExtent, 각 축 반경) 안에서 가장 가까운 NavMesh 폴리곤 위의 점을 out* 에 채운다.
    // NavMesh 미설정/미준비거나 박스 안에 폴리곤이 없으면 false (out* 미변경).
    bool SampleNavMeshPosition(float x, float y, float z,
                               float halfExtentX, float halfExtentY, float halfExtentZ,
                               float& outX, float& outY, float& outZ) const;

    // GameServer 주입. 생성 직후 소유자가 설정한다.
    // Stage 파생 클래스가 패킷 전송 등을 위해 사용.
    void          SetGameServer(GameServer* pGameServer) { m_pGameServer = pGameServer; }
    GameServer*   GetGameServer() const                  { return m_pGameServer; }

    // 외부 스레드에서 시스템 메시지를 push (thread-safe).
    // 다음 OnUpdate에서 처리된다.
    void      EnqueueMessage(StageMessage msg);

    // 객체의 새 좌표 기준으로 sector 소속이 바뀌었으면 갱신한다.
    // Character::Update 등 이동 처리 이후 호출. sector 변경 없으면 no-op.
    // visibility 갱신은 D-3에서 추가 예정.
    void      UpdateObjectSector(StageObject* pObject);

    // objectId 로 StageObject 를 조회한다 (통합 컨테이너 m_objects 기준). 없으면 nullptr.
    // 비소유 raw 포인터 — 컨텐츠 스레드에서 해당 tick 내 사용 (몬스터 AI 의 타겟 해소 등).
    StageObject* FindObject(int64 objectId);

    // (centerX, centerZ) sector를 중심으로 range 거리 내의 sector를 순회.
    // range=1이면 3x3, range=2면 5x5. 맵 범위 밖 sector는 자동 스킵.
    // 콜백은 Sector*를 받는다 (nullptr은 전달되지 않음).
    template <typename Func>
    void ForEachAdjacentSector(int32 centerX, int32 centerZ, int32 range, Func&& callback)
    {
        for (int32 dz = -range; dz <= range; ++dz)
        {
            const int32 z = centerZ + dz;
            if (z < 0 || z >= m_sectorCountZ)
                continue;
            for (int32 dx = -range; dx <= range; ++dx)
            {
                const int32 x = centerX + dx;
                if (x < 0 || x >= m_sectorCountX)
                    continue;
                callback(&m_sectors[sectorIndexToFlat(x, z)]);
            }
        }
    }

protected:
    // serverbase::Contents 훅
    void OnStart()              override;
    void OnUpdate(int64 deltaMs) override;
    void OnStop()               override;

    // Stage 파생 클래스의 매 tick 로직.
    // OnUpdate 안에서 시스템 메시지 처리 이후에 호출된다.
    virtual void OnStageUpdate(int64 deltaMs) {}

    // ── 몬스터 스폰/디스폰 ─────────────────────────────────────
    // SpawnMonster: monsterKey 의 GameData_Monster 로 Monster 를 생성하여 Stage 객체 컨테이너
    // (m_objects, m_monsterObjects) 와 해당 좌표의 sector 에 등록한다.
    // 그리고 주변 AOI 의 유저들에게 ObjectVisibilityNtf(monster_spawns)로 spawn 을 통보한다.
    // ObjectId 는 ObjectIdGenerator(GameServer 경유)로 발급한다.
    // 성공 시 생성된 Monster*, 실패 시 nullptr (소유권은 Stage 가 가짐).
    Monster* SpawnMonster(int64 monsterKey, float posX, float posY, float posZ, float yaw);

    // DespawnMonster: objectId 의 몬스터를 컨테이너/sector 에서 제거하고,
    // 주변 AOI 의 유저들에게 ObjectVisibilityNtf(despawn_ids)로 despawn 을 통보한다.
    // 성공 시 true, 해당 몬스터가 없으면 false.
    bool DespawnMonster(int64 objectId);

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

    // m_userObjects 순회하면서 Character::Update 호출 + sector 갱신.
    void updateCharacters(int64 deltaMs);

    // m_monsterObjects 순회하면서 Monster::Update(FSM) 호출.
    // (이동 시 sector 갱신은 Monster 내부에서 한다. 컨텐츠 스레드 전용.)
    void updateMonsters(int64 deltaMs);

    // Character의 현재 이동 상태를 그 주변 sector의 모든 캐릭터(자기 포함)에게 MoveNtf로 알린다.
    // 이동 시작/정지/도착 등 *상태 변화 시점*에서 호출.
    void broadcastMoveNtf(const Character& character);

    // Monster 버전. 몬스터는 클라 요청 없이 FSM 이 서버에서 이동을 구동하므로,
    // updateMonsters 에서 이동 상태 변화(ConsumeMoveStateDirty)가 감지될 때 호출한다.
    void broadcastMoveNtf(const Monster& monster);

    // 위 두 broadcastMoveNtf 의 공용 구현. 주어진 이동 상태를 (sectorX, sectorZ) 주변 AOI 의
    // 모든 유저에게 MoveNtf 로 전송한다. objectId 로 클라가 대상 오브젝트(캐릭터/몬스터)를 식별한다.
    void sendMoveNtfToAoi(int64 objectId, int32 sectorX, int32 sectorZ,
                          float posX, float posY, float posZ, float yaw,
                          float destX, float destY, float destZ, bool isMoving);

    // Character가 sector를 바꿔을 때 visibility 갱신.
    // oldAOI − newAOI 안의 캐릭터들에게는 despawn (나, 상대 서로),
    // newAOI − oldAOI 안의 캐릭터들에게는 spawn (나, 상대 서로) 전송.
    void updateVisibilityOnSectorChange(Character& character,
                                        int32 oldSectorX, int32 oldSectorZ,
                                        int32 newSectorX, int32 newSectorZ);

    // Monster가 sector를 바꿨을 때 visibility 갱신 (단방향: 유저에게만 통보).
    // newAOI − oldAOI 안의 유저들에게는 이 몬스터 spawn (이동 중이면 직후 MoveNtf 도),
    // oldAOI − newAOI 안의 유저들에게는 이 몬스터 despawn 전송.
    // 몬스터는 관찰자가 아니므로 캐릭터판과 달리 서로 교환하지 않는다.
    void updateMonsterVisibilityOnSectorChange(Monster& monster,
                                               int32 oldSectorX, int32 oldSectorZ,
                                               int32 newSectorX, int32 newSectorZ);

    // 섹터 그리드 초기화 (생성자에서 1회 호출)
    void initializeSectorGrid();

    // 2D 섹터 좌표 → 1D 배열 인덱스 변환
    int32 sectorIndexToFlat(int32 sectorX, int32 sectorZ) const { return sectorZ * m_sectorCountX + sectorX; }

    // 객체를 자신의 현재 좌표 기준으로 sector에 등록.
    // pObject->m_curSectorX/Y를 갱신. 맵 범위 밖이면 등록 안 함.
    void addObjectToSector(StageObject* pObject);

    // 객체를 현재 등록된 sector에서 제거. curSectorX/Y를 -1로 설정.
    void removeObjectFromSector(StageObject* pObject);

private:
    int64      m_stageId   = 0;
    EStageType m_stageType = EStageType::None;
	const GameData_Stage* m_pStageData = nullptr;   // 현재 Stage의 데이터. 반드시 null이 아님.

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
    double m_worldMinZ = 0.0;
    double m_worldMaxX = 0.0;
    double m_worldMaxZ = 0.0;
    double m_sectorSize = 0.0;
    int32  m_sectorCountX = 0;
    int32  m_sectorCountZ = 0;

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

    // ── NavMesh ────────────────────────────────────────────
    // 길찾기 로직 + dtNavMeshQuery/dtQueryFilter lifetime 은 StageNavMesh 가 담당.
    // Stage 가 nullptr 이면 NavMesh 미설정 상태 (길찾기 비활성화).
    std::unique_ptr<StageNavMesh> m_pStageNavMesh;
};

using StagePtr  = std::shared_ptr<Stage>;
using StageWPtr = std::weak_ptr<Stage>;
