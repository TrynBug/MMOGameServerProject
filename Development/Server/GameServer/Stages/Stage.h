#pragma once

#include "pch.h"
#include "User.h"
#include "Stages/Sector.h"
#include "StageObjects/StageObject.h"
#include "GameServerDefine.h"

#include "Enum/GameEnum_Stage.h"
#include "Generated/GameData_Stage.h"

#include <variant>

// Detour forward declaration.
// SetNavMesh 가 const dtNavMesh* 를 받기 위해서만 필요. 다른 dtXxx 는 StageNavMesh 가 흡수.
class dtNavMesh;

// NavMesh 길찾기 헬퍼는 StageNavMesh 로 분리되어 있다 (Sector 처럼).
// 헤더에서는 전방선언만, 멤버는 unique_ptr 로 보관 (Stage 소멸자가 .cpp 에 있어야 함).
class StageNavMesh;

// 배치데이터(에디터 export) 로더 + 몬스터 스폰 관리자. StageNavMesh 와 동일하게 unique_ptr 보관.
class StageLayout;
class MonsterSpawner;

// Stage 로직 Lua 스크립트 실행기. unique_ptr 보관(소멸자는 .cpp).
class StageScript;

// 이벤트영역(StageObject 파생). shared_ptr 보관이라 헤더에서는 전방선언만.
class EventArea;

// Character forward declaration (StageMsg_UserEnter 등에서 사용).
class Character;
using CharacterPtr = std::shared_ptr<Character>;

// Monster forward declaration (SpawnMonster 리턴 타입). 완전타입은 Stage.cpp 에서 include.
class Monster;

// ActorObject forward declaration (BroadcastBuff* takes const ActorObject&). Full type in Stage.cpp.
class ActorObject;

// 코루틴 resume executor 전방선언 (포인터 멤버/접근자에만 사용).
namespace db { class IResumeExecutor; }

class Stage;

// 코루틴 후속작업 동안 Stage와 (선택적으로) Character를 고정하는 RAII 핀.
//   - 생성: 코루틴 prologue(첫 co_await 이전, Stage 스레드)에서 카운터 증가.
//   - 소멸: 코루틴 프레임 종료(후속작업 완료, Stage 스레드)에서 카운터 감소.
//   카운터 접근이 모두 Stage 스레드라 lock 불필요. move-only.
//   사용: auto pin = pStage->PinForAsync(spChar); ... co_await ...;  (프레임 종료 시 자동 해제)
//   효과: 핀이 살아있는 동안 Stage 파괴 보류, 해당 Character의 Stage 제거 보류 + 다른 Stage 이동 거부.
class AsyncPin
{
public:
    AsyncPin() = default;                          // 빈 핀(no-op)
    AsyncPin(Stage* pStage, CharacterPtr spChar);  // 정의는 Stage.cpp (Stage/Character 완전타입 필요)
    ~AsyncPin();
    AsyncPin(AsyncPin&& other) noexcept;           // 소유권 이동(원본 무력화)
    AsyncPin& operator=(AsyncPin&&) = delete;
    AsyncPin(const AsyncPin&) = delete;
    AsyncPin& operator=(const AsyncPin&) = delete;

private:
    Stage*       m_pStage = nullptr;
    CharacterPtr m_spChar;
};

// 스킬/효과 시스템 타입 전방선언. 완전 정의는 Stage.cpp 에서 include.
// (EffectShape/EffectParams/AreaEffect/ProjectileGroup = Skill/. Vector3 는 위에서 include 함.)
struct EffectShape;
struct EffectParams;
class AreaEffect;
class ProjectileGroup;
class MonsterProjectile;


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
StageGridParams LoadStageGridParams(int32 stageDataKey);

// ─────────────────────────────────────────────────────────────
// Stage 시스템 메시지
// 외부 스레드(IOCP Worker)에서 Stage에 비동기로 알림을 전달할 때 사용.
// std::variant로 표현하여 핸들러 분기를 깔끔하게 처리한다.
// ─────────────────────────────────────────────────────────────

// 유저 입장.
// 캐릭터 없이 유저만 Stage에 등록된다 (이후 클라 패킷은 이 Stage가 drain).
// 캐릭터 스폰은 별도 단계: 유저가 Moving 상태로 pendingCharacter를 들고 있다가,
// 클라가 StageLoadCompleteReq를 보내면 spawnPendingCharacter로 스폰된다 (2단계 입장).
// SystemStage는 캐릭터가 아예 없으므로 이 메시지만으로 입장이 끝난다.
struct StageMsg_UserEnter
{
    UserPtr spUser;
};

// 유저 퇴장 (게이트웨이로부터 GatewayUserDisconnectNtf 수신 시,
// 또는 다른 Stage로 이동할 때)
struct StageMsg_UserLeave
{
    int64 accountId = 0;
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
class Stage : public serverbase::Contents
{
    // MonsterSpawner 가 SpawnMonster/DespawnMonster(protected) 를 직접 호출한다 (Stage 가 소유, 단일 스레드).
    friend class MonsterSpawner;
    // StageScript(Lua) 가 스폰/스포너/조회 API 를 위해 Stage 내부에 접근한다 (Stage 가 소유, 단일 스레드).
    friend class StageScript;

public:
    // 명시적 grid 값으로 생성.
    Stage(int64 stageId, int32 stageDataKey, EStageType stageType,
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
    int32 	   GetStageDataKey() const { return m_pStageData->Key; }
	const GameData_Stage* pGetStageData() const { return m_pStageData; }

    // 이 Stage가 배정된 컨텐츠 스레드의 resume executor (StageManager가 등록 시 주입, 소유하지 않음).
    // Stage에서 시작한 DB 코루틴의 후속작업을 이 Stage의 스레드에서 재개시키기 위해 ExecuteAsync에 넘긴다.
    void                 SetResumeExecutor(db::IResumeExecutor* p) { m_pResumeExecutor = p; }
    db::IResumeExecutor* GetResumeExecutor() const { return m_pResumeExecutor; }

    // 이 Stage에서 시작하는 코루틴 후속작업용 핀을 만든다. 코루틴 prologue(첫 co_await 이전)에서 잡는다.
    // 핀이 살아있는 동안 이 Stage는 파괴되지 않고, spChar(있으면)는 Stage에서 제거/이동되지 않는다.
    [[nodiscard]] AsyncPin PinForAsync(const CharacterPtr& spChar);

    // 진행 중인 코루틴 후속작업(핀)이 하나라도 있는지. (Stage 파괴 게이트에서 사용)
    bool HasInFlightAsync() const { return m_inFlightAsync > 0; }

    // 진행 중인 코루틴 후속작업 수 증감. AsyncPin(RAII)이 호출한다(직접 호출 금지). Stage 스레드 전용.
    void IncInFlightAsync() { ++m_inFlightAsync; }
    void DecInFlightAsync() { --m_inFlightAsync; }

    // serverbase::Contents 훅 override. 진행 중인 코루틴 후속작업이 있으면 ContentsThread가 제거를 미룬다.
    bool IsBusy() const override { return HasInFlightAsync(); }

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
    bool FindPath(float startX, float startY, float startZ, float endX, float endY, float endZ, std::vector<float>& outWaypoints) const;

    // NavMesh 가 설정/준비되어 길찾기·스냅이 가능한 상태인지.
    bool HasNavMesh() const;

    // 점을 NavMesh 표면으로 스냅한다 (Y 보정 + walkable 검증). StageNavMesh 에 위임.
    // 검색 박스(halfExtent, 각 축 반경) 안에서 가장 가까운 NavMesh 폴리곤 위의 점을 out* 에 채운다.
    // NavMesh 미설정/미준비거나 박스 안에 폴리곤이 없으면 false (out* 미변경).
    bool SampleNavMeshPosition(float x, float y, float z,
                               float halfExtentX, float halfExtentY, float halfExtentZ,
                               float& outX, float& outY, float& outZ) const;

    // (cx,cy,cz) 중심 반경 radius 안의 walkable 랜덤 좌표를 찾는다 (밀도존 스폰 배치용). StageNavMesh 에 위임.
    // NavMesh 미설정/미준비거나 반경 내 폴리곤이 없으면 false (out* 미변경).
    bool SampleRandomNavPoint(float cx, float cy, float cz, float radius,
                              float& outX, float& outY, float& outZ) const;

    // 외부 스레드에서 시스템 메시지를 push (thread-safe).
    // 다음 OnUpdate에서 처리된다.
    void      EnqueueMessage(StageMessage msg);

    // 객체의 새 좌표 기준으로 sector 소속이 바뀌었으면 갱신한다.
    // Character::Update 등 이동 처리 이후 호출. sector 변경 없으면 no-op.
    // visibility 갱신은 D-3에서 추가 예정.
    void      UpdateObjectSector(StageObject* pObject);

    // Buff badge broadcast to AOI users (mirrors MoveNtf broadcast). Called by BuffComponent.
    // remainMs: -1 means permanent (client shows no countdown).
    void      BroadcastBuffNtf(const ActorObject& actor, int32 buffKey, int32 stackCount, int32 remainMs);
    void      BroadcastBuffRemoveNtf(const ActorObject& actor, int32 buffKey);

    // 스킬 시전 통보(SkillCastNtf)를 시전자 주변 AOI 에 broadcast 한다. SkillComponent 가 entry 페이즈 발동 시 호출.
    // effectId: entry 가 투사체면 그 그룹 ID, 아니면 0. 클라는 origin/dir/seed 로 비주얼을 재현한다.
    void      BroadcastSkillCastNtf(const ActorObject& caster, int32 skillKey, int64 effectId,
                                    const Vector3& origin, const Vector3& dir, uint32 seed,
                                    float moveDistance);

    // 능력 시전 "시작" 통보(AbilityCastNtf)를 시전자 주변 AOI 에 broadcast 한다. Monster::TryBeginCast 가 Windup 진입 시 호출.
    // 클라는 caster 를 origin/dir 로 회전 고정 + skill_key 로 윈드업 모션/텔레그래프를 windupMs 동안 재생한다. (몬스터/NPC/엘리트 공용)
    void      BroadcastAbilityCastNtf(const ActorObject& caster, int32 skillKey, int64 targetObjectId,
                                      const Vector3& origin, const Vector3& dir, int32 windupMs);

    // Stage 공지 배너를 Stage 내 "전체" 유저에게 송신 (스크립트 Notice()). AOI 가 아니라 스테이지 전역.
    void      BroadcastStageNoticeNtf(const std::string& message, int32 durationMs);

    // objectId 로 StageObject 를 조회한다 (통합 컨테이너 m_objects 기준). 없으면 nullptr.
    // 비소유 raw 포인터 — 컨텐츠 스레드에서 해당 tick 내 사용 (몬스터 AI 의 타겟 해소 등).
    StageObject* FindObject(int64 objectId);

    // (centerPos 를 중심으로) shape 범위 안에 있는 "적" StageObject 들을 outEnemies 에 채운다 (X-Z 평면).
    // 진영 규칙(v1): casterType 이 Monster 면 User(캐릭터)들을, 그 외(User 등)면 Monster 들을 대상으로 한다.
    // casterType 을 값으로 받는 이유: 효과가 지속되는 동안 시전자 객체가 사라져도 안전하게 하기 위함.
    // shape 의 bounding 반경으로 후보 섹터를 추린 뒤 정밀 판정한다. outEnemies 는 먼저 clear 된다.
    // 비소유 raw 포인터 — 해당 tick 내 사용 (컨텐츠 스레드 전용).
    void QueryEnemiesInShape(EObjectType casterType, const Vector3& centerPos, const EffectShape& shape, std::vector<StageObject*>& outEnemies);

    // 스킬 효과(범위 대미지) 하나를 월드에 시작시킨다. SkillComponent 가 bake 한 EffectParams 를 넘긴다.
    // (투사체=ProjectileGroup 은 별도 경로. 이 함수는 Area 효과 전용.)
    void SpawnSkillAreaEffect(const EffectParams& params);

    // 서버 권위 몬스터 투사체 하나를 월드에 시작시킨다 (매 tick 서버가 전진+충돌 판정).
    // Monster::ExecuteSkill 이 ContactHit 능력 발동 시 호출. (플레이어 투사체와 달리 클라 hit 보고 없음.)
    void SpawnMonsterProjectile(const EffectParams& params);

    // 효과(스킬)에 의한 대미지를 target 에 적용하고, 주변 AOI 에 SkillDamageNtf 를 broadcast 한다. AreaEffect 등이 호출.
    // isDuplicate: 중복타격으로 감소된 대미지인지 (표시용 flag). 사망 처리(디스폰)는 후속.
    // sourceSkillKey: 대미지를 낸 능력의 GameData_Skill.Key (클라 피격 연출 분기용. 없으면 0).
    void ApplyEffectDamage(ActorObject& target, double damage, int64 killerObjectId, bool isDuplicate = false, int32 sourceSkillKey = 0);

    // 오브젝트 사망을 주변 AOI 유저들에게 통보(ObjectDeathNtf). 클라 사망 연출용.
    // 디스폰(시체 제거)은 corpse 시간 경과 후 DespawnMonster(ObjectVisibilityNtf despawn)로 별도 처리된다.
    void BroadcastObjectDeathNtf(const ActorObject& actor, int64 killerObjectId);

    // 스킬 투사체 묶음(1회 시전 분)을 월드에 등록한다. 발급된 effectId 를 리턴한다 (시전 Ntf 에 실을 용도).
    // dirs: 부채꼴 전개된 투사체별 방향. SkillComponent 가 계산해 넘긴다.
    int64 SpawnSkillProjectileGroup(const EffectParams& params, const std::vector<Vector3>& dirs);

    // 클라가 보고한 투사체 종료 사건을 처리한다 (직격 대미지 + 폭발). 폭발 적중은 서버가 판정.
    //   - 직격: targetId 유효 + flag 없음 → projPos 에서 직격 대미지 + 폭발.
    //   - 최대사거리(explodedAtMaxRange): origin+dir*maxRange 에 폭발만.
    //   - 지형(explodedOnTerrain): (hitX, hitZ) 에 폭발만.
    void OnSkillProjectileHit(int64 effectId, int32 projectileIndex, int64 targetId,
                              bool explodedAtMaxRange, bool explodedOnTerrain,
                              float hitX, float hitZ);

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

    // (centerSectorX, centerSectorZ) 주변 AOI(k_aoiRange) 안의 모든 유저에 대해 콜백을 호출한다.
    // 콜백 시그니처: void(int64 accountId). center 가 -1(맵 밖)이면 아무것도 하지 않는다.
    // "내 주변 유저 전체에게 패킷 전송" 브로드캐스트의 공통 골격. (ForEachAdjacentSector 와 동일 성능, std::function 없음)
    template <typename Func>
    void ForEachUserInAoi(int32 centerSectorX, int32 centerSectorZ, Func&& callback)
    {
        if (centerSectorX < 0 || centerSectorZ < 0)
            return;

        ForEachAdjacentSector(centerSectorX, centerSectorZ, k_aoiRange,
            [&](Sector* pSector)
            {
                for (const auto& [objId, pObj] : pSector->GetUsers())
                    callback(pObj->GetOwnerAccountId());
            });
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
    Monster* SpawnMonster(int32 monsterKey, float posX, float posY, float posZ, float yaw);

    // DespawnMonster: objectId 의 몬스터를 컨테이너/sector 에서 제거하고,
    // 주변 AOI 의 유저들에게 ObjectVisibilityNtf(despawn_ids)로 despawn 을 통보한다.
    // 성공 시 true, 해당 몬스터가 없으면 false.
    bool DespawnMonster(int64 objectId);

    // ── 시스템 메시지 처리 hooks (파생 클래스가 override 가능) ──
    // 기본 동작: 유저를 m_users에 추가/제거 (캐릭터 스폰은 spawnPendingCharacter 별도 단계).
    virtual void OnUserEnter(const UserPtr& spUser);
    virtual void OnUserLeave(int64 accountId);

    // 유저의 pendingCharacter를 이 Stage에 스폰한다 (StageLoadCompleteReq 수신 시 호출).
    // - pendingPositionType != None이면 StageStartPosition 데이터의 좌표로 배치 (NavMesh 스냅 보정),
    //   None이면 캐릭터의 현재 좌표 사용 (캐릭터 선택 입장 = DB 좌표 복귀).
    // - 객체 컨테이너/sector 등록 + AOI visibility 전파 + StageLoadCompleteRes 송신까지 수행.
    // - 성공 시 유저 상태를 InStage로 전환하고 pendingCharacter를 비운다.
    // 스폰 전/후 서브클래스 확장점은 OnResolveSpawnTransform / OnCharacterSpawned 참조.
    void spawnPendingCharacter(const UserPtr& spUser);

    // ── StageLoadCompleteReq 처리(spawnPendingCharacter) 중 호출되는 서브클래스 확장 훅 ──
    // 모두 컨텐츠 스레드에서 호출되며, 기본 구현은 no-op이다.

    // 스폰 직전: 도착 위치/회전을 서브클래스가 보정할 수 있다 (StageStartPosition 기본값 위에 덮어쓰기).
    // posX/Y/Z, yaw는 in/out 파라미터. 예: 던전이 동적으로 계산한 입구 좌표로 변경.
    virtual void OnResolveSpawnTransform(const CharacterPtr& spCharacter, EStagePositionType positionType,
                                         float& posX, float& posY, float& posZ, float& yaw) {}

    // 스폰 완료 후: 캐릭터가 Stage에 등록되고 클라/주변 AOI에 통보된 뒤 호출.
    // 예: 입장 버프 부여, NPC/이벤트 트리거.
    virtual void OnCharacterSpawned(const UserPtr& spUser, const CharacterPtr& spCharacter) {}

    // 유저가 클라이언트로부터 보낸 패킷 처리 진입점.
    // 패킷ID로 핸들러 테이블(getUserPacketHandlerMap)을 조회하여 해당 멤버 핸들러를 호출한다.
    // 미등록 패킷은 Debug 로그만 남긴다.
    virtual void OnUserPacket(const UserPtr& spUser, const netlib::PacketPtr& spPacket);

    // ── 유저 패킷 핸들러 디스패치 ──────────────────────────────
    // 패킷ID → 멤버 핸들러 포인터. 모든 핸들러는 (UserPtr, PacketPtr) 시그니처로 통일한다.
    using UserPacketHandler    = void (Stage::*)(const UserPtr&, const netlib::PacketPtr&);
    using UserPacketHandlerMap = std::unordered_map<uint16, UserPacketHandler>;

    // 이 Stage 종류가 처리할 [패킷ID, 핸들러] 테이블을 돌려준다.
    // static const 테이블이라 클래스당 한 번만 생성된다 (Stage 인스턴스마다 드는 비용 없음).
    // 파생 클래스가 자기만의 패킷셋을 가지려면 이 함수를 override 하여 자기 테이블을 돌려준다.
    virtual const UserPacketHandlerMap& getUserPacketHandlerMap() const;

    // 패킷 payload 를 TMsg 로 역직렬화한다. 실패 시 Warn 로그 후 false (호출자는 return).
    // 각 핸들러에 중복되던 역직렬화 + 실패 로그 보일러플레이트를 흡수한다.
    template <typename TMsg>
    bool deserializeUserPacket(const UserPtr& spUser, const netlib::PacketPtr& spPacket, TMsg& outMsg);

    // 개별 패킷 핸들러. getUserPacketHandlerMap 테이블에 등록되어 OnUserPacket 이 호출한다.
    void handleMoveIntentReq(const UserPtr& spUser, const netlib::PacketPtr& spPacket);
    void handleSkillCastReq(const UserPtr& spUser, const netlib::PacketPtr& spPacket);
    void handleSkillProjectileHitReq(const UserPtr& spUser, const netlib::PacketPtr& spPacket);

    // Stage 이동 요청 (동일/크로스 서버 통합. 현재는 동일 서버만 처리, 크로스서버는 Phase 2).
    // old Stage(=this)의 컨텐츠 스레드에서 실행된다. 검증 → leave → pending 보관 → target enter → Res.
    void handleStageMoveReq(const UserPtr& spUser, const netlib::PacketPtr& spPacket);

    // 클라 맵 로딩 완료 보고. target Stage(=this)의 컨텐츠 스레드에서 실행된다.
    // Moving 상태의 유저면 spawnPendingCharacter로 스폰한다.
    void handleStageLoadCompleteReq(const UserPtr& spUser, const netlib::PacketPtr& spPacket);

    // [치트] 클라 치트 콘솔의 서버치트 요청(CheatReq) 처리. _DEBUG 빌드에서만 테이블에 등록된다.
    // name 으로 치트 핸들러를 찾아 실행하고 CheatRes 로 결과 메시지를 회신한다 (StageCheat.cpp / Stage.cpp).
    void handleCheatReq(const UserPtr& spUser, const netlib::PacketPtr& spPacket);

    // 이벤트영역 진입/이탈 선보고(클라 -> 서버). 권위 위치로 검증 후 스크립트 콜백 발동.
    // 단, secure 영역은 이 보고를 무시하고 pollSecureEventAreas 가 매 tick 권위 위치로 직접 판정한다.
    void handleEventAreaEnterReq(const UserPtr& spUser, const netlib::PacketPtr& spPacket);
    void handleEventAreaExitReq(const UserPtr& spUser, const netlib::PacketPtr& spPacket);

    // secure 이벤트영역을 매 tick 서버 권위 위치로 폴링해 진입/이탈 콜백을 발동(OnUpdate 4.7).
    void pollSecureEventAreas();

    // 오브젝트(prop) 상호작용 요청(클라 -> 서버). 권위 위치가 marker 범위 안인지 검증 후 OnObjectInteract 발동.
    void handleObjectInteractReq(const UserPtr& spUser, const netlib::PacketPtr& spPacket);

    // ── 유저 컨테이너 접근 ──
    const std::unordered_map<int64, UserPtr>& GetUsers() const { return m_users; }

private:
    // 시스템 메시지 큐 처리
    void processSystemMessages();

    // 각 유저의 클라 패킷 큐 drain 및 처리
    void processUserPackets();

    // m_characterObjects 순회하면서 Character::Update 호출 + sector 갱신.
    void updateCharacters(int64 deltaMs);

    // m_monsterObjects 순회하면서 Monster::Update(FSM) 호출.
    // (이동 시 sector 갱신은 Monster 내부에서 한다. 컨텐츠 스레드 전용.)
    void updateMonsters(int64 deltaMs);

    // 진행 중인 스킬 효과(AreaEffect)들을 tick 하고 만료된 것을 제거한다. OnUpdate 에서 매 tick 호출.
    void updateSkillEffects(int64 deltaMs);

    // 매 tick AOI 스냅샷 송신 (이동 복제의 중심). 각 유저에게 자기 AOI 내 보이는 오브젝트
    // (캐릭터/몬스터)의 현재 권위 상태(위치/yaw/flags)를 SnapshotNtf 로 모아 unicast 한다.
    // 클라는 원격 액터를 이 스냅샷들로 보간하고, 본인 캐릭터는 화해에 사용한다.
    void buildAndSendSnapshots();

    // 저빈도(k_timeSyncPeriodTicks)로 stage 내 전 유저에게 TimeSyncNtf 를 broadcast 한다.
    // 클라 NetClock 재생 시계의 권위 앵커. SnapshotNtf 와 독립이라 SnapshotNtf 가 0건이어도 시계가 굶지 않는다.
    void buildAndSendTimeSync();

#ifdef _DEBUG
    // [디버그 UI] 구독 중인 유저에게 디버그 패킷을 주기적으로 push (개발용).
    // dbgstat 구독자에게 DebugStatNtf, dbgmon 구독자에게 DebugMonsterPositionsNtf 를 보낸다.
    // OnUpdate 끝에서 호출되며, 디버그 tick 주기로만 실제 전송한다.
    void sendDebugSubscriptions();
#endif

    // effectId 로 진행 중인 투사체 그룹을 찾는다. 없으면 nullptr.
    ProjectileGroup* findSkillProjectileGroup(int64 effectId);

    // Character가 sector를 바꿔을 때 visibility 갱신.
    // oldAOI − newAOI 안의 캐릭터들에게는 despawn (나, 상대 서로),
    // newAOI − oldAOI 안의 캐릭터들에게는 spawn (나, 상대 서로) 전송.
    void updateVisibilityOnSectorChange(Character& character, int32 oldSectorX, int32 oldSectorZ, int32 newSectorX, int32 newSectorZ);

    // Monster가 sector를 바꿨을 때 visibility 갱신 (단방향: 유저에게만 통보).
    // newAOI − oldAOI 안의 유저들에게는 이 몬스터 spawn (이동 중이면 직후 MoveNtf 도),
    // oldAOI − newAOI 안의 유저들에게는 이 몬스터 despawn 전송.
    // 몬스터는 관찰자가 아니므로 캐릭터판과 달리 서로 교환하지 않는다.
    void updateMonsterVisibilityOnSectorChange(Monster& monster, int32 oldSectorX, int32 oldSectorZ, int32 newSectorX, int32 newSectorZ);

    // 섹터 그리드 초기화 (생성자에서 1회 호출)
    void initializeSectorGrid();

    // 2D 섹터 좌표 → 1D 배열 인덱스 변환
    int32 sectorIndexToFlat(int32 sectorX, int32 sectorZ) const { return sectorZ * m_sectorCountX + sectorX; }

    // StageObject 를 Stage 에 등록하는 공용 진입점. 모든 등록 경로(유저 입장/몬스터 스폰 등)가
    // 반드시 이 함수를 거친다. updateIntervalMs 는 필수 인자 — 오브젝트 종류에 맞는 업데이트
    // 주기를 호출자가 명시적으로 지정해야 한다 (자동 기본값 없음).
    // 통합 컨테이너(m_objects)와 전달된 타입별 맵에 등록하고, Stage/업데이트주기/sector 를 설정한다.
    void registerObject(const StageObjectPtr& spObject, int64 updateIntervalMs, std::unordered_map<int64, StageObjectPtr>& typeMap);

    // 객체를 자신의 현재 좌표 기준으로 sector에 등록.
    // pObject->m_curSectorX/Y를 갱신. 맵 범위 밖이면 등록 안 함.
    void addObjectToSector(StageObject* pObject);

    // 객체를 현재 등록된 sector에서 제거. curSectorX/Y를 -1로 설정.
    void removeObjectFromSector(StageObject* pObject);

    // 보류된 유저 퇴장 처리. 코루틴 후속작업(핀)이 걸려있어 미뤄둔 OnUserLeave를, 핀이 풀린 유저에 한해 실행.
    // OnUpdate 시작부에서 매 tick 호출.
    void processPendingLeaves();

private:
    int64      m_stageId   = 0;
    EStageType m_stageType = EStageType::None;
	const GameData_Stage* m_pStageData = nullptr;   // 현재 Stage의 데이터. 반드시 null이 아님.

    // 배정된 컨텐츠 스레드의 resume executor (StageManager가 등록 시 주입). 소유하지 않음(스레드는 ServerBase 소유).
    db::IResumeExecutor* m_pResumeExecutor = nullptr;

    // 진행 중인 코루틴 후속작업(핀) 수. AsyncPin이 Stage 스레드에서만 증감하므로 atomic/lock 불필요.
    int m_inFlightAsync = 0;

    // 코루틴 후속작업(핀) 때문에 제거를 미뤄둔 유저 ID 목록. processPendingLeaves가 핀 해제 후 처리.
    std::vector<int64> m_pendingLeaves;

    // Stage 단조 증가 시계 (ms). OnUpdate 마다 deltaMs 누적. 스킬 효과/투사체 타이밍 기준.
    int64      m_stageClockMs = 0;

    // 스냅샷 스트리밍용 서버 단조 증가 tick 번호. OnUpdate 마다 +1. 클라 보간 시계 기준이 된다.
    uint32     m_serverTickSeq = 0;

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
    std::unordered_map<int64, UserPtr> m_users;

    // AOI 브로드캐스트 시 수신자 accountId 를 모으는 재사용 버퍼 (컨텐츠 스레드 전용, 호출마다 clear).
    // 매 브로드캐스트마다 vector 를 새로 만들지 않기 위함. 브로드캐스트는 tick 내에서 순차 호출되어 재진입 없음.
    std::vector<int64> m_aoiUserScratch;

    // 스냅샷 송신용 재사용 메시지 (컨텐츠 스레드 전용). 매 tick·매 유저마다 Clear() 후 재사용한다.
    // protobuf Clear() 는 repeated 필드 capacity 를 유지하므로 add_states() 의 재할당을 막는다.
    GamePacket::SnapshotNtf m_snapshotScratch;

    // TimeSyncNtf 수신자 수집용 재사용 버퍼 (컨텐츠 스레드 전용). 매 송신마다 clear() 후 재사용.
    std::vector<int64> m_timeSyncUserScratch;

    // ── StageObject 통합 + 타입별 소유 컨테이너 ──
    // 모든 StageObject의 lifetime은 m_objects가 관리(shared_ptr).
    // 타입별 맵(m_characterObjects 등)은 같은 shared_ptr을 빠른 조회용으로 보관.
    std::unordered_map<int64, StageObjectPtr> m_objects;
    std::unordered_map<int64, StageObjectPtr> m_characterObjects;
    std::unordered_map<int64, StageObjectPtr> m_monsterObjects;
    std::unordered_map<int64, StageObjectPtr> m_propObjects;
    std::unordered_map<int64, StageObjectPtr> m_dropObjects;

    // ── NavMesh ────────────────────────────────────────────
    // 길찾기 로직 + dtNavMeshQuery/dtQueryFilter lifetime 은 StageNavMesh 가 담당.
    // Stage 가 nullptr 이면 NavMesh 미설정 상태 (길찾기 비활성화).
    std::unique_ptr<StageNavMesh> m_pStageNavMesh;

    // ── 배치데이터 + 몬스터 스폰 ───────────────────────────────
    // 레이아웃: StageAssetManager 가 소유하는 공유 불변 데이터를 참조만 한다(인스턴스마다 파싱 안 함).
    // 스포너 관리자는 인스턴스별 런타임 상태라 소유. OnStart 에서 셋업, OnUpdate 에서 tick.
    const StageLayout*              m_pLayout = nullptr;
    std::unique_ptr<MonsterSpawner> m_pSpawner;

    // Stage 로직 스크립트(Lua). 스크립트 파일이 있는 Stage 만 생성된다(없으면 nullptr).
    std::unique_ptr<StageScript>    m_pScript;

    // 이벤트영역(인스턴스별). eventKey -> EventArea 객체(지오메트리 + occupant 상태 보유).
    // OnStart 에서 레이아웃 배치데이터로 생성. AOI 통보 안 함 — sector/m_objects 에 등록하지 않는다.
    // 진입/이탈 핸들러가 권위 위치를 객체의 Contains 로 검증하고 occupant 를 추적한다(§8 클라 선판정).
    std::unordered_map<int32, std::shared_ptr<EventArea>> m_eventAreas;

    // ── 스킬 효과 ──────────────────────────────
    // 진행 중인 범위 효과(AreaEffect)들. updateSkillEffects 에서 tick + 만료 sweep.
    // unique_ptr 보관: Stage.h 가 AreaEffect 완전타입에 의존하지 않도록 (재빌드 영향 최소화).
    std::vector<std::unique_ptr<AreaEffect>> m_skillAreaEffects;

    // 진행 중인 투사체 그룹들 (effectId → 그룹). 클라 hit 보고가 effectId 로 그룹을 지목한다.
    // updateSkillEffects 에서 만료된 그룹을 제거한다.
    std::unordered_map<int64, std::unique_ptr<ProjectileGroup>> m_skillProjectileGroups;

    // 진행 중인 서버 권위 몬스터 투사체들. updateSkillEffects 에서 매 tick 전진+충돌 + 만료 sweep.
    std::vector<std::unique_ptr<MonsterProjectile>> m_monsterProjectiles;
};

using StagePtr  = std::shared_ptr<Stage>;
using StageWPtr = std::weak_ptr<Stage>;
