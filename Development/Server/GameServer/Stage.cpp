#include "pch.h"
#include "Stage.h"
#include "Character.h"   // OnUserEnter에서 Character를 m_objects에 등록하기 위해 완전타입 필요
#include "Monster.h"     // SpawnMonster 에서 Monster 생성 + GameDataTable_Monster 조회를 위해 완전타입 필요
#include "GameServer.h"   // OnUserEnter/OnUserLeave에서 visibility 패킷 전송 위해 필요
#include "StageNavMesh.h"  // SetNavMesh 구현에서 StageNavMesh 완전타입 필요

#include "Generated/GameData_Stage.h"

#include <cmath>

namespace
{
    // Character 정보를 CharacterSpawnInfo (패킷) 형식으로 채웁니다.
    // 좌표계: Unity 와 동일 (X, Y, Z). Y가 높이, X-Z 가 평면.
    GamePacket::CharacterSpawnInfo makeCharacterSpawnInfo(const Character& character)
    {
        const DataStructures::Character& proto = character.GetProto();
        GamePacket::CharacterSpawnInfo info;
        info.set_object_id(character.GetObjectId());
        info.set_owner_user_id(proto.owner_user_id());
        info.set_name(proto.name());
        info.set_job_id(proto.job_id());
        info.set_level(proto.level());
        // 좌표는 런타임이 진실의 원천. StageObject에서 가져온다.
        info.set_pos_x(character.GetPosX());
        info.set_pos_y(character.GetPosY());
        info.set_pos_z(character.GetPosZ());
        info.set_yaw(character.GetYaw());
        return info;
    }

    // Monster 정보를 MonsterSpawnInfo (패킷) 형식으로 채웁니다.
    // 클라는 monster_key 로 게임데이터를 조회해 프리팹 경로 등 나머지 정보를 얻는다.
    // 좌표계: Unity 와 동일 (X, Y, Z). Y가 높이, X-Z 가 평면.
    GamePacket::MonsterSpawnInfo makeMonsterSpawnInfo(const Monster& monster)
    {
        GamePacket::MonsterSpawnInfo info;
        info.set_object_id(monster.GetObjectId());
        info.set_monster_key(monster.GetMonsterData()->Key);
        info.set_pos_x(monster.GetPosX());
        info.set_pos_y(monster.GetPosY());
        info.set_pos_z(monster.GetPosZ());
        info.set_yaw(monster.GetYaw());
        return info;
    }

    // 몬스터 스폰 시 NavMesh 표면 검색 반경(각 축 half-extent).
    // 입력 Y 를 신뢰할 수 없어(예: 데이터에 Y 미입력) Y 검색은 넉넉히 잡아 바닥으로 스냅한다.
    constexpr float k_spawnSampleHalfExtentXZ = 5.0f;
    constexpr float k_spawnSampleHalfExtentY  = 1000.0f;

    constexpr int64 k_heartbeatIntervalMs = 5000;   // 5초마다 1번 heartbeat 로그

    // GameData_Stage 에는 worldMin/Max 이 없습니다 (NavMesh 메타에서 가져옵). 그래서 LoadStageGridParams 의
    // 기본값으로 아래 fallback 을 쓴다. 조건:
    //   - NavMesh 가 필요한 Stage(Town 등)는 생성 과정에서 NavMeshManager 의 NavMeshMeta 로
    //     bounds 를 덮어쓰므로 이 fallback 은 사용되지 않는다.
    //   - NavMesh 가 필요없는 Stage(SystemStage 등)는 sector grid 자체를 쓰지 않으므로
    //     bounds 값이 무엇이든 완전히 안전하다. 그래도 initializeSectorGrid 이 에러 로그를
    //     남기지 않도록 수치적으로 유효한 값으로 둔다.
    constexpr double k_fallbackWorldMinX  = -500.0;
    constexpr double k_fallbackWorldMinZ  = -500.0;
    constexpr double k_fallbackWorldMaxX  =  500.0;
    constexpr double k_fallbackWorldMaxZ  =  500.0;
    constexpr double k_fallbackSectorSize =   50.0;
}

StageGridParams LoadStageGridParams(int64 stageId)
{
    StageGridParams params;
    // worldMin/Max 는 fallback 으로 기본 세팅. NavMesh 가 있는 Stage 는 호출자가 NavMeshMeta 로 덮어쓴다.
    params.worldMinX = k_fallbackWorldMinX;
    params.worldMinZ = k_fallbackWorldMinZ;
    params.worldMaxX = k_fallbackWorldMaxX;
    params.worldMaxZ = k_fallbackWorldMaxZ;

    const GameData_Stage* pData = GameDataTable_Stage::FindData(stageId);
    if (!pData)
    {
        LOG_WRITE(LogLevel::Error, std::format("LoadStageGridParams: GameData_Stage not found. stageId={}. using fallback values.", stageId));
        params.stageType  = EStageType::None;
        params.navMeshFileName.clear();
        params.sectorSize = k_fallbackSectorSize;
    }
    else
    {
        params.stageType       = pData->StageType;
        params.navMeshFileName = pData->NavMeshFileName;
        params.sectorSize      = pData->sectorSize;
    }
    return params;
}

Stage::Stage(int64 stageId, EStageType stageType,
             double worldMinX, double worldMinZ,
             double worldMaxX, double worldMaxZ,
             double sectorSize)
    : m_stageId(stageId)
    , m_stageType(stageType)
    , m_worldMinX(worldMinX)
    , m_worldMinZ(worldMinZ)
    , m_worldMaxX(worldMaxX)
    , m_worldMaxZ(worldMaxZ)
    , m_sectorSize(sectorSize)
{
    initializeSectorGrid();
}

Stage::~Stage()
{
    // m_pStageNavMesh 의 unique_ptr 소멸자가 자동으로 StageNavMesh::~StageNavMesh() 호출.
    // StageNavMesh 의 완전타입을 .cpp 에서만 알 수 있으므로 destructor 는 여기에 있어야 한다.
}

void Stage::SetNavMesh(const dtNavMesh* pNavMesh)
{
    // 이전것 정리 후 새로 생성.
    // pNavMesh 가 nullptr 이면 StageNavMesh 생성자가 IsReady()=false 로 둘다.
    m_pStageNavMesh = std::make_unique<StageNavMesh>(pNavMesh);

    LOG_WRITE(LogLevel::Info, std::format("Stage::SetNavMesh - stageId={} ready={}",
        m_stageId, m_pStageNavMesh->IsReady()));
}

bool Stage::FindPath(float startX, float startY, float startZ,
                     float endX,   float endY,   float endZ,
                     std::vector<float>& outWaypoints) const
{
    outWaypoints.clear();
    if (!m_pStageNavMesh)
    {
        LOG_WRITE(LogLevel::Warn, std::format("Stage::FindPath - StageNavMesh not set. stageId={}", m_stageId));
        return false;
    }
    return m_pStageNavMesh->FindPath(startX, startY, startZ, endX, endY, endZ, outWaypoints);
}

bool Stage::HasNavMesh() const
{
    return m_pStageNavMesh && m_pStageNavMesh->IsReady();
}

bool Stage::SampleNavMeshPosition(float x, float y, float z,
                                  float halfExtentX, float halfExtentY, float halfExtentZ,
                                  float& outX, float& outY, float& outZ) const
{
    if (!m_pStageNavMesh)
        return false;
    return m_pStageNavMesh->SamplePosition(x, y, z, halfExtentX, halfExtentY, halfExtentZ, outX, outY, outZ);
}

void Stage::initializeSectorGrid()
{
    // 입력값 검증
    if (m_sectorSize <= 0.0)
    {
        LOG_WRITE(LogLevel::Error, std::format("Stage::initializeSectorGrid - invalid sectorSize={}. stageId={}",
            m_sectorSize, m_stageId));
        return;
    }

    if (m_worldMaxX <= m_worldMinX || m_worldMaxZ <= m_worldMinZ)
    {
        LOG_WRITE(LogLevel::Error, std::format("Stage::initializeSectorGrid - invalid world bounds. stageId={} min=({},{}) max=({},{})",
            m_stageId, m_worldMinX, m_worldMinZ, m_worldMaxX, m_worldMaxZ));
        return;
    }

    // 섹터 개수 계산 (ceil로 올림하여 맵 영역이 섹터로 빠짐없이 커버되도록).
    const double worldSizeX = m_worldMaxX - m_worldMinX;
    const double worldSizeZ = m_worldMaxZ - m_worldMinZ;
    m_sectorCountX = static_cast<int32>(std::ceil(worldSizeX / m_sectorSize));
    m_sectorCountZ = static_cast<int32>(std::ceil(worldSizeZ / m_sectorSize));

    if (m_sectorCountX <= 0 || m_sectorCountZ <= 0)
    {
        LOG_WRITE(LogLevel::Error, std::format("Stage::initializeSectorGrid - sector count <= 0. stageId={} count=({}x{})",
            m_stageId, m_sectorCountX, m_sectorCountZ));
        return;
    }

    // 섹터 그리드 생성 + 각 섹터에 인덱스 설정
    const int32 totalSectors = m_sectorCountX * m_sectorCountZ;
    m_sectors.resize(totalSectors);

    for (int32 z = 0; z < m_sectorCountZ; ++z)
    {
        for (int32 x = 0; x < m_sectorCountX; ++x)
        {
            m_sectors[sectorIndexToFlat(x, z)].SetIndex(x, z);
        }
    }

    LOG_WRITE(LogLevel::Info, std::format("Stage::initializeSectorGrid - stageId={} world=({},{})~({},{}) sectorSize={} grid={}x{} totalSectors={}",
        m_stageId,
        m_worldMinX, m_worldMinZ, m_worldMaxX, m_worldMaxZ,
        m_sectorSize, m_sectorCountX, m_sectorCountZ, totalSectors));
}

bool Stage::GetSectorIndex(float posX, float posZ, int32& outSectorX, int32& outSectorZ) const
{
    if (m_sectorSize <= 0.0)
        return false;

    // float와 double 비교: float가 double로 묵시적으로 승격. 경계는 정확히 처리됨.
    if (posX < m_worldMinX || posX >= m_worldMaxX || posZ < m_worldMinZ || posZ >= m_worldMaxZ)
        return false;

    // 계산은 double로 수행하고 결과만 int32로 캐스팅.
    outSectorX = static_cast<int32>((static_cast<double>(posX) - m_worldMinX) / m_sectorSize);
    outSectorZ = static_cast<int32>((static_cast<double>(posZ) - m_worldMinZ) / m_sectorSize);

    // 부동소수점 오차로 인한 경계값 안전장치
    if (outSectorX >= m_sectorCountX) outSectorX = m_sectorCountX - 1;
    if (outSectorZ >= m_sectorCountZ) outSectorZ = m_sectorCountZ - 1;

    return true;
}

bool Stage::IsValidSectorIndex(int32 sectorX, int32 sectorZ) const
{
    return sectorX >= 0 && sectorX < m_sectorCountX
        && sectorZ >= 0 && sectorZ < m_sectorCountZ;
}

Sector* Stage::GetSector(int32 sectorX, int32 sectorZ)
{
    if (!IsValidSectorIndex(sectorX, sectorZ))
        return nullptr;
    return &m_sectors[sectorIndexToFlat(sectorX, sectorZ)];
}

const Sector* Stage::GetSector(int32 sectorX, int32 sectorZ) const
{
    if (!IsValidSectorIndex(sectorX, sectorZ))
        return nullptr;
    return &m_sectors[sectorIndexToFlat(sectorX, sectorZ)];
}

Sector* Stage::GetSectorByPos(float posX, float posZ)
{
    int32 sectorX = 0;
    int32 sectorZ = 0;
    if (!GetSectorIndex(posX, posZ, sectorX, sectorZ))
        return nullptr;
    return GetSector(sectorX, sectorZ);
}

void Stage::OnStart()
{
    LOG_WRITE(LogLevel::Info, std::format("Stage::OnStart - stageId={} stageType={}",
        m_stageId, static_cast<int>(m_stageType)));
}

void Stage::OnUpdate(int64 deltaMs)
{
    // 1. 시스템 메시지 처리 (유저 입장/퇴장 등)
    processSystemMessages();

    // 2. 각 유저의 클라 패킷 처리 (유저별로 queue drain)
    processUserPackets();

    // 3. 캐릭터 이동 시뮬레이션 + sector 갱신
    updateCharacters(deltaMs);

    // 4. 파생 클래스 로직
    OnStageUpdate(deltaMs);

    // 5. heartbeat 로그 (5초마다 1번)
    m_heartbeatAccumMs += deltaMs;
    if (m_heartbeatAccumMs >= k_heartbeatIntervalMs)
    {
        m_heartbeatAccumMs = 0;
        LOG_WRITE(LogLevel::Debug, std::format("Stage heartbeat. stageId={} stageType={} userCount={}",
            m_stageId, static_cast<int>(m_stageType), m_users.size()));
    }
}

void Stage::OnStop()
{
    LOG_WRITE(LogLevel::Info, std::format("Stage::OnStop - stageId={} stageType={} userCount={}",
        m_stageId, static_cast<int>(m_stageType), m_users.size()));

    // 남아있는 유저들은 그대로 두고 종료. GameServer 종료 흐름에서 별도 처리됨.
    m_users.clear();
}

void Stage::EnqueueMessage(StageMessage msg)
{
    std::lock_guard<std::mutex> lock(m_pendingMessagesMutex);
    m_pendingMessages.push_back(std::move(msg));
}

Monster* Stage::SpawnMonster(int64 monsterKey, float posX, float posY, float posZ, float yaw)
{
    const GameData_Monster* pMonsterData = GameDataTable_Monster::FindData(monsterKey);
    if (!pMonsterData)
    {
        LOG_WRITE(LogLevel::Error, std::format("Stage::SpawnMonster - GameData_Monster not found. stageId={} monsterKey={}",
            m_stageId, monsterKey));
        return nullptr;
    }

    GameServer* pServer = GetGameServer();
    if (!pServer)
    {
        LOG_WRITE(LogLevel::Error, std::format("Stage::SpawnMonster - GameServer not injected. stageId={} monsterKey={}",
            m_stageId, monsterKey));
        return nullptr;
    }

    // ── 스폰 위치 검증 + NavMesh Y 스냅 ──────────────────────
    // NavMesh 가 있으면 표면으로 스냅(Y 보정 + walkable 검증). 검색 박스 안에 walkable 폴리곤이
    // 없으면(=걸을 수 없는 위치) 스폰을 거부한다. 입력 Y 를 신뢰할 수 없어 Y 검색 반경은 넉넉히.
    // NavMesh 가 없는 Stage(예: SystemStage)는 스냅 없이 월드 경계만 검증한다.
    float spawnX = posX;
    float spawnY = posY;
    float spawnZ = posZ;

    if (HasNavMesh())
    {
        float snappedX = 0.0f;
        float snappedY = 0.0f;
        float snappedZ = 0.0f;
        if (!SampleNavMeshPosition(posX, posY, posZ,
                k_spawnSampleHalfExtentXZ, k_spawnSampleHalfExtentY, k_spawnSampleHalfExtentXZ,
                snappedX, snappedY, snappedZ))
        {
            LOG_WRITE(LogLevel::Error, std::format("Stage::SpawnMonster - spawn pos not on NavMesh, rejected. stageId={} monsterKey={} pos=({},{},{})",
                m_stageId, monsterKey, posX, posY, posZ));
            return nullptr;
        }
        spawnX = snappedX;
        spawnY = snappedY;
        spawnZ = snappedZ;
    }

    // 월드 경계 밖이면 sector 에 등록되지 못해 유령 객체가 되므로 스폰을 거부한다.
    int32 spawnSectorX = 0;
    int32 spawnSectorZ = 0;
    if (!GetSectorIndex(spawnX, spawnZ, spawnSectorX, spawnSectorZ))
    {
        LOG_WRITE(LogLevel::Error, std::format("Stage::SpawnMonster - spawn pos out of world bounds, rejected. stageId={} monsterKey={} pos=({},{},{})",
            m_stageId, monsterKey, spawnX, spawnY, spawnZ));
        return nullptr;
    }

    // ObjectId 발급 후 Monster 생성. Monster 생성자가 종류데이터 기본스탯 적용 + 현재HP/MP 풀피까지 처리한다.
    const int64 objectId = pServer->GenerateObjectId();
    MonsterPtr spMonster = std::make_shared<Monster>(objectId, pMonsterData);
    spMonster->SetPos(spawnX, spawnY, spawnZ);
    spMonster->SetYaw(yaw);
    spMonster->SetStage(this);

    // Stage 객체 컨테이너 등록 (통합 + 타입별).
    m_objects[objectId] = spMonster;
    m_monsterObjects[objectId] = spMonster;

    // 좌표 기준 sector 등록. 맵 범위 밖이면 (-1,-1) 로 남고 addObjectToSector 가 경고 로그를 남긴다.
    addObjectToSector(spMonster.get());

    LOG_WRITE(LogLevel::Info, std::format("Stage::SpawnMonster - stageId={} monsterKey={} objectId={} pos=({},{},{}) yaw={} sector=({},{}) totalObjects={}",
        m_stageId, monsterKey, objectId, spawnX, spawnY, spawnZ, yaw,
        spMonster->GetCurSectorX(), spMonster->GetCurSectorZ(), m_objects.size()));

    // 주변 sector AOI 안의 유저들에게 spawn 통보. (몬스터는 관찰자가 아니므로 단방향.)
    const int32 sx = spMonster->GetCurSectorX();
    const int32 sz = spMonster->GetCurSectorZ();
    if (sx >= 0 && sz >= 0)
    {
        std::vector<GamePacket::MonsterSpawnInfo> singleMonster = { makeMonsterSpawnInfo(*spMonster) };
        ForEachAdjacentSector(sx, sz, k_aoiRange,
            [&](Sector* pSector)
            {
                for (const auto& [otherObjId, pOtherObj] : pSector->GetUsers())
                {
                    Character* pOtherChar = static_cast<Character*>(pOtherObj);
                    const int64 otherUserId = pOtherChar->GetProto().owner_user_id();
                    pServer->SendObjectVisibilityNtf(otherUserId, {}, {}, singleMonster);
                }
            });
    }

    return spMonster.get();
}

bool Stage::DespawnMonster(int64 objectId)
{
    auto iter = m_monsterObjects.find(objectId);
    if (iter == m_monsterObjects.end())
    {
        LOG_WRITE(LogLevel::Warn, std::format("Stage::DespawnMonster - monster not found. stageId={} objectId={}",
            m_stageId, objectId));
        return false;
    }

    StageObjectPtr spMonster = iter->second;

    // 제거 전에 sector 좌표를 캐시 (despawn broadcast 범위 결정용).
    const int32 sx = spMonster->GetCurSectorX();
    const int32 sz = spMonster->GetCurSectorZ();

    // sector 및 컨테이너에서 제거.
    removeObjectFromSector(spMonster.get());
    m_monsterObjects.erase(iter);
    m_objects.erase(objectId);

    LOG_WRITE(LogLevel::Info, std::format("Stage::DespawnMonster - stageId={} objectId={} totalObjects={}",
        m_stageId, objectId, m_objects.size()));

    // 주변 sector AOI 안의 유저들에게 despawn 통보.
    if (GameServer* pServer = GetGameServer())
    {
        if (sx >= 0 && sz >= 0)
        {
            std::vector<int64> despawnIds = { objectId };
            ForEachAdjacentSector(sx, sz, k_aoiRange,
                [&](Sector* pSector)
                {
                    for (const auto& [otherObjId, pOtherObj] : pSector->GetUsers())
                    {
                        Character* pOtherChar = static_cast<Character*>(pOtherObj);
                        const int64 otherUserId = pOtherChar->GetProto().owner_user_id();
                        pServer->SendObjectVisibilityNtf(otherUserId, {}, despawnIds);
                    }
                });
        }
    }

    return true;
}

// ── sector 등록/제거/이동 헬퍼 ─────────────────────────────
// 섹터는 X-Z 평면으로만 분할됨. Y(높이)는 섹터 분할에 사용 안 함.

void Stage::addObjectToSector(StageObject* pObject)
{
    if (!pObject)
        return;

    int32 sectorX = 0;
    int32 sectorZ = 0;
    if (!GetSectorIndex(pObject->GetPosX(), pObject->GetPosZ(), sectorX, sectorZ))
    {
        // 맵 범위 밖. 섹터에 등록 안 함. (-1, -1)로 유지.
        LOG_WRITE(LogLevel::Warn, std::format("Stage::addObjectToSector - object pos out of world bounds. stageId={} objectId={} pos=({},{},{})",
            m_stageId, pObject->GetObjectId(), pObject->GetPosX(), pObject->GetPosY(), pObject->GetPosZ()));
        pObject->SetCurSector(-1, -1);
        return;
    }

    Sector* pSector = GetSector(sectorX, sectorZ);
    if (!pSector)
    {
        LOG_WRITE(LogLevel::Error, std::format("Stage::addObjectToSector - sector not found. stageId={} sector=({},{})",
            m_stageId, sectorX, sectorZ));
        pObject->SetCurSector(-1, -1);
        return;
    }

    pSector->AddObject(pObject);
    pObject->SetCurSector(sectorX, sectorZ);
}

void Stage::removeObjectFromSector(StageObject* pObject)
{
    if (!pObject)
        return;

    const int32 sectorX = pObject->GetCurSectorX();
    const int32 sectorZ = pObject->GetCurSectorZ();
    if (sectorX < 0 || sectorZ < 0)
        return;   // 섹터에 속한 적 없음.

    Sector* pSector = GetSector(sectorX, sectorZ);
    if (pSector)
    {
        pSector->RemoveObject(pObject);
    }
    pObject->SetCurSector(-1, -1);
}

void Stage::UpdateObjectSector(StageObject* pObject)
{
    if (!pObject)
        return;

    int32 newSectorX = 0;
    int32 newSectorZ = 0;
    const bool bInsideWorld = GetSectorIndex(pObject->GetPosX(), pObject->GetPosZ(), newSectorX, newSectorZ);

    const int32 oldSectorX = pObject->GetCurSectorX();
    const int32 oldSectorZ = pObject->GetCurSectorZ();

    if (!bInsideWorld)
    {
        // 맵 범위 밖으로 나갔으면 섹터에서 빼고 끝.
        if (oldSectorX >= 0 && oldSectorZ >= 0)
        {
            if (Sector* pOldSector = GetSector(oldSectorX, oldSectorZ))
                pOldSector->RemoveObject(pObject);
            pObject->SetCurSector(-1, -1);
        }
        return;
    }

    // sector 변경 없으면 no-op.
    if (oldSectorX == newSectorX && oldSectorZ == newSectorZ)
        return;

    // 이전 sector에서 제거 (있었다면) → 새 sector에 등록.
    if (oldSectorX >= 0 && oldSectorZ >= 0)
    {
        if (Sector* pOldSector = GetSector(oldSectorX, oldSectorZ))
            pOldSector->RemoveObject(pObject);
    }
    if (Sector* pNewSector = GetSector(newSectorX, newSectorZ))
    {
        pNewSector->AddObject(pObject);
    }
    pObject->SetCurSector(newSectorX, newSectorZ);

    LOG_WRITE(LogLevel::Debug, std::format("Stage::UpdateObjectSector - stageId={} objectId={} ({},{}) -> ({},{})",
        m_stageId, pObject->GetObjectId(), oldSectorX, oldSectorZ, newSectorX, newSectorZ));
}

void Stage::processSystemMessages()
{
    // 락 잠깐만 잡고 swap. 처리 자체는 락 밖에서 수행.
    std::vector<StageMessage> messages;
    {
        std::lock_guard<std::mutex> lock(m_pendingMessagesMutex);
        if (m_pendingMessages.empty())
            return;
        messages.swap(m_pendingMessages);
    }

    for (auto& msg : messages)
    {
        std::visit([this](auto&& m)
        {
            using T = std::decay_t<decltype(m)>;
            if constexpr (std::is_same_v<T, StageMsg_UserEnter>)
            {
                OnUserEnter(m.spUser, m.spCharacter);
            }
            else if constexpr (std::is_same_v<T, StageMsg_UserLeave>)
            {
                OnUserLeave(m.userId);
            }
        }, msg);
    }
}

void Stage::OnUserEnter(const UserPtr& spUser, const CharacterPtr& spCharacter)
{
    if (!spUser)
        return;

    const int64 userId = spUser->GetUserId();
    m_users[userId] = spUser;
    spUser->SetCurrentStageId(m_stageId);

    // 캐릭터가 함께 입장하면 Stage 객체 컨테이너에도 등록.
    if (spCharacter)
    {
        const int64 objectId = spCharacter->GetObjectId();
        m_objects[objectId] = spCharacter;
        m_userObjects[objectId] = spCharacter;
        spCharacter->SetStage(this);

        // sector에도 등록 (좌표 기준). 맵 범위 밖이면 (-1, -1).
        addObjectToSector(spCharacter.get());

        LOG_WRITE(LogLevel::Info, std::format("Stage::OnUserEnter - stageId={} userId={} characterId={} sector=({},{}) totalUsers={} totalObjects={}",
            m_stageId, userId, objectId,
            spCharacter->GetCurSectorX(), spCharacter->GetCurSectorZ(),
            m_users.size(), m_objects.size()));

        // ── visibility 전파 ────────────────────────────────────
        // 주변 sector AOI 기반.
        if (GameServer* pServer = GetGameServer())
        {
            const GamePacket::CharacterSpawnInfo myInfo = makeCharacterSpawnInfo(*spCharacter);

            // 나에게 전송할 spawn 목록 (주변 sector의 모든 캐릭터(자기 포함) + 모든 몬스터).
            std::vector<GamePacket::CharacterSpawnInfo> spawnsForMe;
            spawnsForMe.reserve(16);
            std::vector<GamePacket::MonsterSpawnInfo> monsterSpawnsForMe;
            monsterSpawnsForMe.reserve(16);

            // 다른 캐릭터에게 전송할 용도의 "내 spawn 1개".
            std::vector<GamePacket::CharacterSpawnInfo> singleSpawn = { myInfo };

            ForEachAdjacentSector(spCharacter->GetCurSectorX(), spCharacter->GetCurSectorZ(), k_aoiRange,
                [&](Sector* pSector)
                {
                    for (const auto& [otherObjId, pOtherObj] : pSector->GetUsers())
                    {
                        Character* pOtherChar = static_cast<Character*>(pOtherObj);
                        spawnsForMe.push_back(makeCharacterSpawnInfo(*pOtherChar));

                        // 다른 캐릭터에게 내 spawn 전송 (자기 자신은 제외).
                        if (otherObjId != objectId)
                        {
                            const int64 otherUserId = pOtherChar->GetProto().owner_user_id();
                            pServer->SendObjectVisibilityNtf(otherUserId, singleSpawn, {});
                        }
                    }

                    // 주변 몬스터도 나에게 spawn 통보. (몬스터는 관찰자가 아니므로 받기만 한다.)
                    for (const auto& [monsterObjId, pMonsterObj] : pSector->GetMonsters())
                    {
                        monsterSpawnsForMe.push_back(makeMonsterSpawnInfo(*static_cast<Monster*>(pMonsterObj)));
                    }
                });

            pServer->SendObjectVisibilityNtf(userId, spawnsForMe, {}, monsterSpawnsForMe);
        }
    }
    else
    {
        LOG_WRITE(LogLevel::Info, std::format("Stage::OnUserEnter - stageId={} userId={} (no character) totalUsers={}",
            m_stageId, userId, m_users.size()));
    }
}

void Stage::OnUserLeave(int64 userId)
{
    auto iter = m_users.find(userId);
    if (iter == m_users.end())
    {
        LOG_WRITE(LogLevel::Warn, std::format("Stage::OnUserLeave - user not found. stageId={} userId={}",
            m_stageId, userId));
        return;
    }

    // User에 연결된 Character가 있으면 m_objects/m_userObjects에서 제거.
    int64 leavingObjectId = 0;
    int32 leavingSectorX  = -1;
    int32 leavingSectorZ  = -1;
    UserPtr spUser = iter->second;
    if (spUser)
    {
        CharacterPtr spCharacter = spUser->GetCurrentCharacter();
        if (spCharacter && spCharacter->GetStage() == this)
        {
            leavingObjectId = spCharacter->GetObjectId();

            // 제거 전에 sector 좌표를 캐시 (despawn broadcast 범위 결정용).
            leavingSectorX = spCharacter->GetCurSectorX();
            leavingSectorZ = spCharacter->GetCurSectorZ();

            // sector에서 먼저 제거.
            removeObjectFromSector(spCharacter.get());

            m_userObjects.erase(leavingObjectId);
            m_objects.erase(leavingObjectId);
            spCharacter->SetStage(nullptr);
        }
    }

    m_users.erase(iter);

    LOG_WRITE(LogLevel::Info, std::format("Stage::OnUserLeave - stageId={} userId={} totalUsers={} totalObjects={}",
        m_stageId, userId, m_users.size(), m_objects.size()));

    // 주변 sector의 다른 캐릭터들에게 despawn broadcast.
    if (leavingObjectId != 0)
    {
        if (GameServer* pServer = GetGameServer())
        {
            std::vector<int64> despawnIds = { leavingObjectId };

            ForEachAdjacentSector(leavingSectorX, leavingSectorZ, k_aoiRange,
                [&](Sector* pSector)
                {
                    for (const auto& [otherObjId, pOtherObj] : pSector->GetUsers())
                    {
                        Character* pOtherChar = static_cast<Character*>(pOtherObj);
                        const int64 otherUserId = pOtherChar->GetProto().owner_user_id();
                        pServer->SendObjectVisibilityNtf(otherUserId, {}, despawnIds);
                    }
                });
        }
    }
}

void Stage::OnUserPacket(const UserPtr& spUser, const netlib::PacketPtr& spPacket)
{
    if (!spUser || !spPacket)
        return;

    const uint16 packetType = spPacket->GetHeader()->type;

    // User의 현재 캐릭터 lock. Character 단계 패킷 (이동 등)은 캐릭터가 있어야 처리 가능.
    CharacterPtr spCharacter = spUser->GetCurrentCharacter();

    switch (packetType)
    {
    case Common::GAME_PACKET_ID_MOVE_DEST_REQ:
    {
        if (!spCharacter || spCharacter->GetStage() != this)
        {
            LOG_WRITE(LogLevel::Warn, std::format("Stage::OnUserPacket - MoveDestReq but no character or wrong stage. stageId={} userId={}",
                m_stageId, spUser->GetUserId()));
            return;
        }
        GamePacket::MoveDestReq req;
        if (!GetGameServer() || !GetGameServer()->DeserializePacket(*spPacket, req))
        {
            LOG_WRITE(LogLevel::Warn, std::format("Stage::OnUserPacket - failed to deserialize MoveDestReq. stageId={} userId={}",
                m_stageId, spUser->GetUserId()));
            return;
        }
        spCharacter->SetDestination(req.dest_x(), req.dest_y(), req.dest_z());
        broadcastMoveNtf(*spCharacter);
        return;
    }
    case Common::GAME_PACKET_ID_MOVE_STOP_REQ:
    {
        if (!spCharacter || spCharacter->GetStage() != this)
        {
            LOG_WRITE(LogLevel::Warn, std::format("Stage::OnUserPacket - MoveStopReq but no character or wrong stage. stageId={} userId={}",
                m_stageId, spUser->GetUserId()));
            return;
        }
        GamePacket::MoveStopReq req;
        if (!GetGameServer() || !GetGameServer()->DeserializePacket(*spPacket, req))
        {
            LOG_WRITE(LogLevel::Warn, std::format("Stage::OnUserPacket - failed to deserialize MoveStopReq. stageId={} userId={}",
                m_stageId, spUser->GetUserId()));
            return;
        }

        // 클라/서버 위치 오차 검증. X-Z 평면 거리로 계산 (Y는 NavMesh가 결정하므로 비교 제외).
        // 오차 범위 내면 클라 위치 인정, 초과면 서버 위치로 고정.
        const float dx = req.pos_x() - spCharacter->GetPosX();
        const float dz = req.pos_z() - spCharacter->GetPosZ();
        const float distSq = dx * dx + dz * dz;
        const float tolSq  = k_movePositionTolerance * k_movePositionTolerance;

        if (distSq <= tolSq)
        {
            spCharacter->StopAt(req.pos_x(), req.pos_y(), req.pos_z(), req.yaw());
        }
        else
        {
            // 서버 위치로 고정. 향후 위치 보정 패킷 추가 예정.
            LOG_WRITE(LogLevel::Warn, std::format("Stage::OnUserPacket - MoveStopReq position out of tolerance. stageId={} userId={} clientPos=({},{},{}) serverPos=({},{},{})",
                m_stageId, spUser->GetUserId(),
                req.pos_x(), req.pos_y(), req.pos_z(),
                spCharacter->GetPosX(), spCharacter->GetPosY(), spCharacter->GetPosZ()));
            spCharacter->StopAt(spCharacter->GetPosX(), spCharacter->GetPosY(), spCharacter->GetPosZ(), req.yaw());
        }
        broadcastMoveNtf(*spCharacter);
        return;
    }
    default:
        break;
    }

    // 처리되지 않은 패킷은 디버그 로그.
    LOG_WRITE(LogLevel::Debug, std::format("Stage::OnUserPacket - unhandled. stageId={} userId={} packetType={} payloadSize={}",
        m_stageId, spUser->GetUserId(),
        packetType, spPacket->GetPayloadSize()));
}

void Stage::processUserPackets()
{
    // 각 유저의 패킷 큐를 drain하여 OnUserPacket 호출.
    // 이 루프는 Stage 스레드 전용 접근 구간이므로 m_users에 안전하게 접근 가능.
    std::vector<netlib::PacketPtr> packets;
    for (auto& [userId, spUser] : m_users)
    {
        spUser->DrainPackets(packets);
        if (packets.empty())
            continue;

        for (auto& spPacket : packets)
        {
            OnUserPacket(spUser, spPacket);
        }
        packets.clear();
    }
}

void Stage::updateCharacters(int64 deltaMs)
{
    // m_userObjects의 모든 Character를 이동 시뮬레이션 한 다음 sector 소속 갱신.
    // 현재 m_userObjects에 들어가는 객체는 모두 Character (EObjectType::User)이므로 static_cast 안전.
    for (auto& [objectId, spObject] : m_userObjects)
    {
        Character* pCharacter = static_cast<Character*>(spObject.get());

        // Update 전 sector 좌표 캐치 (sector 변경 감지용).
        const int32 oldSectorX = pCharacter->GetCurSectorX();
        const int32 oldSectorZ = pCharacter->GetCurSectorZ();

        const bool arrived = pCharacter->Update(deltaMs);

        // 좌표가 바뀌었을 수 있으면 sector 갱신 (도착했거나 이동 중 모두).
        UpdateObjectSector(pCharacter);

        // sector가 바뀐 경우 visibility 갱신.
        const int32 newSectorX = pCharacter->GetCurSectorX();
        const int32 newSectorZ = pCharacter->GetCurSectorZ();
        if (oldSectorX != newSectorX || oldSectorZ != newSectorZ)
        {
            updateVisibilityOnSectorChange(*pCharacter, oldSectorX, oldSectorZ, newSectorX, newSectorZ);
        }

        // 도착했으면 (이동 → 정지로 상태 변경) 주변에 MoveNtf 알림.
        if (arrived)
        {
            broadcastMoveNtf(*pCharacter);
        }
    }
}

void Stage::broadcastMoveNtf(const Character& character)
{
    GameServer* pServer = GetGameServer();
    if (!pServer)
        return;

    const int64 objectId = character.GetObjectId();
    const float posX     = character.GetPosX();
    const float posY     = character.GetPosY();
    const float posZ     = character.GetPosZ();
    const float yaw      = character.GetYaw();
    const float destX    = character.GetDestX();
    const float destY    = character.GetDestY();
    const float destZ    = character.GetDestZ();
    const bool  isMoving = character.IsMoving();

    // 주변 sector의 모든 캐릭터(자기 자신 포함)에게 unicast.
    // 캐릭터의 현재 sector가 -1이면(맵 밖) broadcast 안 함.
    const int32 sx = character.GetCurSectorX();
    const int32 sz = character.GetCurSectorZ();
    if (sx < 0 || sz < 0)
        return;

    ForEachAdjacentSector(sx, sz, k_aoiRange,
        [&](Sector* pSector)
        {
            for (const auto& [otherObjId, pOtherObj] : pSector->GetUsers())
            {
                Character* pOtherChar = static_cast<Character*>(pOtherObj);
                const int64 otherUserId = pOtherChar->GetProto().owner_user_id();
                pServer->SendMoveNtf(otherUserId, objectId, posX, posY, posZ, yaw, destX, destY, destZ, isMoving);
            }
        });
}

void Stage::updateVisibilityOnSectorChange(Character& character,
                                           int32 oldSectorX, int32 oldSectorZ,
                                           int32 newSectorX, int32 newSectorZ)
{
    GameServer* pServer = GetGameServer();
    if (!pServer)
        return;

    const int64 myObjectId = character.GetObjectId();
    const int64 myUserId   = character.GetProto().owner_user_id();
    const GamePacket::CharacterSpawnInfo myInfo = makeCharacterSpawnInfo(character);

    // sector (x, z)가 (centerX, centerZ) 기준 k_aoiRange 범위 안에 있는지 검사.
    // 단, sector 좌표가 -1이면 (맵 밖) 어떤 범위에도 속하지 않음.
    auto inAOI = [](int32 x, int32 z, int32 centerX, int32 centerZ) -> bool
    {
        if (centerX < 0 || centerZ < 0)
            return false;
        return std::abs(x - centerX) <= k_aoiRange && std::abs(z - centerZ) <= k_aoiRange;
    };

    // ── newAOI 순회 ──
    // oldAOI에 없던 sector(=새로 보임)의 캐릭터들에게 spawn 교환.
    // 자기 자신은 제외 (이미 m_userObjects/newSector에 있고, 본인은 spawn 알 필요 없음).
    std::vector<GamePacket::CharacterSpawnInfo> newlyVisibleSpawnsForMe;
    newlyVisibleSpawnsForMe.reserve(8);
    std::vector<GamePacket::MonsterSpawnInfo> newlyVisibleMonstersForMe;
    newlyVisibleMonstersForMe.reserve(8);
    std::vector<GamePacket::CharacterSpawnInfo> singleSpawnOfMe = { myInfo };

    ForEachAdjacentSector(newSectorX, newSectorZ, k_aoiRange,
        [&](Sector* pSector)
        {
            // 이 sector가 oldAOI에도 있으면 skip (계속 보이는 영역).
            const int32 sx = pSector->GetSectorX();
            const int32 sz = pSector->GetSectorZ();
            if (inAOI(sx, sz, oldSectorX, oldSectorZ))
                return;

            for (const auto& [otherObjId, pOtherObj] : pSector->GetUsers())
            {
                if (otherObjId == myObjectId)
                    continue;   // 자기 자신은 자기 sector(newSector 안)에 있을 거고, spawn 보낼 필요 없음.

                Character* pOtherChar = static_cast<Character*>(pOtherObj);
                newlyVisibleSpawnsForMe.push_back(makeCharacterSpawnInfo(*pOtherChar));

                const int64 otherUserId = pOtherChar->GetProto().owner_user_id();
                pServer->SendObjectVisibilityNtf(otherUserId, singleSpawnOfMe, {});
            }

            // 새로 보이는 sector의 몬스터들도 나에게 spawn. (몬스터는 받기만 한다.)
            for (const auto& [monsterObjId, pMonsterObj] : pSector->GetMonsters())
            {
                newlyVisibleMonstersForMe.push_back(makeMonsterSpawnInfo(*static_cast<Monster*>(pMonsterObj)));
            }
        });

    if (!newlyVisibleSpawnsForMe.empty() || !newlyVisibleMonstersForMe.empty())
    {
        pServer->SendObjectVisibilityNtf(myUserId, newlyVisibleSpawnsForMe, {}, newlyVisibleMonstersForMe);
    }

    // ── oldAOI 순회 ──
    // newAOI에 없는 sector(=더 이상 안 보임)의 캐릭터들에게 despawn 교환.
    std::vector<int64> despawnIdsForMe;
    despawnIdsForMe.reserve(8);
    std::vector<int64> myDespawnId = { myObjectId };

    ForEachAdjacentSector(oldSectorX, oldSectorZ, k_aoiRange,
        [&](Sector* pSector)
        {
            // 이 sector가 newAOI에도 있으면 skip (계속 보이는 영역).
            const int32 sx = pSector->GetSectorX();
            const int32 sz = pSector->GetSectorZ();
            if (inAOI(sx, sz, newSectorX, newSectorZ))
                return;

            for (const auto& [otherObjId, pOtherObj] : pSector->GetUsers())
            {
                if (otherObjId == myObjectId)
                    continue;   // 자기 자신은 이미 newSector로 옮겨졌으므로 oldAOI에서 발견될 일 없지만 방어.

                Character* pOtherChar = static_cast<Character*>(pOtherObj);
                despawnIdsForMe.push_back(otherObjId);

                const int64 otherUserId = pOtherChar->GetProto().owner_user_id();
                pServer->SendObjectVisibilityNtf(otherUserId, {}, myDespawnId);
            }

            // 더 이상 안 보이는 sector의 몬스터들은 나에게 despawn (despawn_ids 는 타입 무관).
            for (const auto& [monsterObjId, pMonsterObj] : pSector->GetMonsters())
            {
                despawnIdsForMe.push_back(monsterObjId);
            }
        });

    if (!despawnIdsForMe.empty())
    {
        pServer->SendObjectVisibilityNtf(myUserId, {}, despawnIdsForMe);
    }
}
