#include "pch.h"
#include "Stage.h"
#include "Character.h"
#include "Monster.h"
#include "MonsterFsmAI.h"
#include "GameServer.h"
#include "StageNavMesh.h"
#include "StageLayout.h"
#include "MonsterSpawner.h"
#include "StageScript.h"
#include "EventArea.h"
#include "Skill/EffectShape.h"
#include "Skill/EffectParams.h"
#include "Skill/AreaEffect.h"
#include "Skill/ProjectileGroup.h"
#include "Skill/MonsterProjectile.h"
#include "Skill/SkillBake.h"
#include "Generated/GameData_Skill.h"
#include "Generated/GameData_StageStartPosition.h"
#include "Enum/GameEnum_Common.h"

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
        info.set_hp(character.GetCurHp());
        info.set_max_hp(character.GetMaxHp());
        info.set_mp(character.GetCurMp());
        info.set_max_mp(character.GetMaxMp());
        // 좌표는 런타임이 진실의 원천. StageObject에서 가져온다.
        info.set_pos_x(character.GetPosX());
        info.set_pos_y(character.GetPosY());
        info.set_pos_z(character.GetPosZ());
        info.set_yaw(character.GetYaw());

        // Fill current buffs into the spawn snapshot (UI badges) so viewers see them on sight.
        character.GetBuffComponent().ForEachBuff(
            [&info](int32 buffKey, int32 stackCount, int32 remainMs)
            {
                GamePacket::BuffSnapshotInfo* pBuff = info.add_buffs();
                pBuff->set_buff_key(buffKey);
                pBuff->set_stack_count(stackCount);
                pBuff->set_remain_time_ms(remainMs);
            });

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
        info.set_is_dead(monster.IsDead());
        info.set_cur_hp(monster.GetCurHp());
        info.set_max_hp(monster.GetMaxHp());

        // Fill current buffs into the spawn snapshot (UI badges) so viewers see them on sight.
        monster.GetBuffComponent().ForEachBuff(
            [&info](int32 buffKey, int32 stackCount, int32 remainMs)
            {
                GamePacket::BuffSnapshotInfo* pBuff = info.add_buffs();
                pBuff->set_buff_key(buffKey);
                pBuff->set_stack_count(stackCount);
                pBuff->set_remain_time_ms(remainMs);
            });

        return info;
    }

    // 몬스터 스폰 시 NavMesh 표면 검색 반경(각 축 half-extent).
    // 입력 Y 를 신뢰할 수 없어(예: 데이터에 Y 미입력) Y 검색은 넉넉히 잡아 바닥으로 스냅한다.
    constexpr float k_spawnSampleHalfExtentXZ = 5.0f;
    constexpr float k_spawnSampleHalfExtentY  = 1000.0f;

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

StageGridParams LoadStageGridParams(int32 stageDataKey)
{
    StageGridParams params;
    // worldMin/Max 는 fallback 으로 기본 세팅. NavMesh 가 있는 Stage 는 호출자가 NavMeshMeta 로 덮어쓴다.
    params.worldMinX = k_fallbackWorldMinX;
    params.worldMinZ = k_fallbackWorldMinZ;
    params.worldMaxX = k_fallbackWorldMaxX;
    params.worldMaxZ = k_fallbackWorldMaxZ;

    const GameData_Stage* pData = GameDataTable_Stage::FindData(stageDataKey);
    if (!pData)
    {
        LOG_WRITE(LogLevel::Error, std::format("GameData_Stage not found. stageDataKey={}. using fallback values.", stageDataKey));
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

Stage::Stage(int64 stageId, int32 stageDataKey, EStageType stageType,
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

	m_pStageData = GameDataTable_Stage::FindData(stageDataKey);
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

    LOG_WRITE(LogLevel::Info, std::format("stageId={} ready={}", m_stageId, m_pStageNavMesh->IsReady()));
}

bool Stage::FindPath(float startX, float startY, float startZ,
                     float endX,   float endY,   float endZ,
                     std::vector<float>& outWaypoints) const
{
    outWaypoints.clear();
    if (!m_pStageNavMesh)
    {
        LOG_WRITE(LogLevel::Warn, std::format("StageNavMesh not set. stageId={}", m_stageId));
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

bool Stage::SampleRandomNavPoint(float cx, float cy, float cz, float radius,
                                 float& outX, float& outY, float& outZ) const
{
    if (!m_pStageNavMesh)
        return false;
    return m_pStageNavMesh->SampleRandomPoint(cx, cy, cz, radius, outX, outY, outZ);
}

StageObject* Stage::FindObject(int64 objectId)
{
    auto it = m_objects.find(objectId);
    if (it == m_objects.end())
        return nullptr;
    return it->second.get();
}

void Stage::BroadcastStageNoticeNtf(const std::string& message, int32 durationMs)
{
    PacketSender& sender = GameServer::Instance().GetPacketSender();
    for (const auto& [userId, spUser] : m_users)
        sender.SendStageNoticeNtf(userId, message, durationMs);
}

void Stage::updateMonsters(int64 deltaMs)
{
    // m_monsterObjects 에는 Monster 만 등록된다 (SpawnMonster). FSM 1 tick 진행.
    // Monster::Update 는 이동 시 내부에서 UpdateObjectSector(sector 맵)를 호출하지만
    // m_monsterObjects 자체는 변경하지 않으므로 순회 중 안전하다.
    // (사망 시 디스폰은 여기서 하지 않는다 — 순회 중 컨테이너 변경 금지.)
    // 사망 만료된 몬스터는 순회 중 erase 하면 안 되므로(컨테이너 변경 금지) 모았다가 루프 후 디스폰한다.
    std::vector<int64> deadToDespawn;

    for (auto& pair : m_monsterObjects)
    {
        Monster* pMonster = static_cast<Monster*>(pair.second.get());

        // 사망한 몬스터: AI/이동/타게팅 정지. corpse 카운트다운만 매 tick 진행하고, 만료되면 디스폰 예약.
        if (pMonster->IsDead())
        {
            if (pMonster->AdvanceCorpseTimer(deltaMs))
                deadToDespawn.push_back(pMonster->GetObjectId());
            continue;
        }

        // 이 몬스터의 업데이트 주기에 도달했는지 확인. 아직이면 이번 tick 은 건너뛴다.
        // elapsedMs = 마지막 Update 이후 누적 경과시간(주기가 길면 deltaMs 보다 크다).
        int64 elapsedMs = 0;
        if (!pMonster->AdvanceUpdateClock(deltaMs, elapsedMs))
            continue;

        // Update 전 sector 좌표 캐치 (sector 변경 감지용). Monster::Update 내부에서
        // UpdateObjectSector 가 호출되어 갱신되므로, Update 후 좌표와 비교한다.
        const int32 oldSectorX = pMonster->GetCurSectorX();
        const int32 oldSectorZ = pMonster->GetCurSectorZ();

        pMonster->Update(elapsedMs);

        // sector 가 바뀌면 visibility 갱신: 새로 보게 된 유저에게 spawn, 못 보게 된 유저에게 despawn.
        const int32 newSectorX = pMonster->GetCurSectorX();
        const int32 newSectorZ = pMonster->GetCurSectorZ();
        if (oldSectorX != newSectorX || oldSectorZ != newSectorZ)
            updateMonsterVisibilityOnSectorChange(*pMonster, oldSectorX, oldSectorZ, newSectorX, newSectorZ);

        // (이동 복제는 buildAndSendSnapshots 의 스냅샷 스트리밍이 담당. 별도 MoveNtf 브로드캐스트 없음.)

        // Buff tick (expire + DoT/HoT). Monster::Update already settled its sector.
        pMonster->GetBuffComponent().Update(elapsedMs);

        // 스킬 체인 진행 (몬스터 시전은 v1 미사용이라 보통 no-op). 일관성을 위해 호출.
        pMonster->GetSkillComponent().Update(elapsedMs);
    }

    // 순회 종료 후 사망 만료 몬스터 디스폰 (기존 ObjectVisibilityNtf despawn 통보 재사용).
    for (int64 objectId : deadToDespawn)
        DespawnMonster(objectId);
}

void Stage::initializeSectorGrid()
{
    // 입력값 검증
    if (m_sectorSize <= 0.0)
    {
        LOG_WRITE(LogLevel::Error, std::format("invalid sectorSize={}. stageId={}", m_sectorSize, m_stageId));
        return;
    }

    if (m_worldMaxX <= m_worldMinX || m_worldMaxZ <= m_worldMinZ)
    {
        LOG_WRITE(LogLevel::Error, std::format("invalid world bounds. stageId={} min=({},{}) max=({},{})",
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
        LOG_WRITE(LogLevel::Error, std::format("sector count <= 0. stageId={} count=({}x{})", m_stageId, m_sectorCountX, m_sectorCountZ));
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

    LOG_WRITE(LogLevel::Info, std::format("stageId={} world=({},{})~({},{}) sectorSize={} grid={}x{} totalSectors={}",
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
    LOG_WRITE(LogLevel::Info, std::format("stageId={} stageType={}", m_stageId, static_cast<int>(m_stageType)));

    // 배치데이터: StageAssetManager 의 공유 불변 레이아웃 참조(인스턴스마다 파싱하지 않음).
    m_pLayout = GameServer::Instance().GetStageAssetManager().FindLayout(GetStageDataKey());

    // 몬스터 스포너 초기화(인스턴스별). 실제 채움은 OnUpdate 의 Update 에서.
    m_pSpawner = std::make_unique<MonsterSpawner>();
    if (m_pLayout)
        m_pSpawner->Load(*this, *m_pLayout);

    // 이벤트영역 객체 생성(인스턴스별). 레이아웃 배치데이터마다 EventArea(StageObject 파생)를 만든다.
    // objectId 는 내부 핸들(네트워크 비노출). AOI 통보 안 함 — sector/m_objects 에 등록하지 않는다.
    if (m_pLayout)
    {
        for (const auto& placement : m_pLayout->GetEventAreas())
        {
            const int64 objectId = GameServer::Instance().GenerateObjectId();
            auto spEventArea = std::make_shared<EventArea>();
            if (!spEventArea->Initialize(objectId, placement))
            {
                LOG_WRITE(LogLevel::Error, std::format("EventArea Initialize failed. stageId={} eventKey={}", m_stageId, placement.key));
                continue;
            }
            spEventArea->SetStage(this);
            m_eventAreas.emplace(placement.key, std::move(spEventArea));
        }
    }

    // Stage 로직 스크립트 로드 (GameData_Stage.ScriptName1~3, 빈 슬롯 제외).
    // 파일 경로는 StageScript 가 Map/StageScript/<이름>.lua 로 해석한다.
    std::vector<std::string> scriptNames;
    for (int32 i = 0; i < m_pStageData->GetScriptNameCount(); ++i)
    {
        std::string name = m_pStageData->GetScriptName(i);
        if (!name.empty())
            scriptNames.push_back(std::move(name));
    }
    if (!scriptNames.empty())
    {
        m_pScript = std::make_unique<StageScript>();
        m_pScript->Load(*this, scriptNames);
        m_pScript->CallOnStageStart();
    }
}

void Stage::OnUpdate(int64 deltaMs)
{
    // 0. Stage 단조 시계 갱신 (스킬 효과/투사체 타이밍 기준).
    m_stageClockMs += deltaMs;
    ++m_serverTickSeq;   // 스냅샷 스트리밍용 tick 번호 (클라 보간 시계 기준).

    // 1. 시스템 메시지 처리 (유저 입장/퇴장 등)
    processSystemMessages();

    // 2. 각 유저의 클라 패킷 처리 (유저별로 queue drain)
    processUserPackets();

    // 3. 캐릭터 이동 시뮬레이션 + sector 갱신
    updateCharacters(deltaMs);

    // 4. 몬스터 AI(FSM) 시뮬레이션 + sector 갱신
    updateMonsters(deltaMs);

    // 4.5 몬스터 스포너 (밀도 유지/리스폰/활성화). 데이터 구동 스폰.
    if (m_pSpawner)
        m_pSpawner->Update(deltaMs);

    // 4.6 Stage 스크립트 (타이머 만기 → Lua 콜백).
    if (m_pScript)
        m_pScript->Update(deltaMs);

    // 4.7 secure 이벤트영역 폴링 (클라 미신뢰 영역만 — 서버가 권위 위치로 직접 진입/이탈 판정).
    if (!m_eventAreas.empty())
        pollSecureEventAreas();

    // 5. 파생 클래스 로직
    OnStageUpdate(deltaMs);

    // 6. 진행 중인 스킬 효과(AreaEffect) tick + 만료 처리
    updateSkillEffects(deltaMs);

    // 6.5 AOI 스냅샷 스트리밍 (이동 복제). 이번 tick 시뮬레이션 결과(위치)를 주변 유저에게 송신.
    buildAndSendSnapshots();
}

void Stage::OnStop()
{
    LOG_WRITE(LogLevel::Info, std::format("stageId={} stageType={} userCount={}",
        m_stageId, static_cast<int>(m_stageType), m_users.size()));

    // 남아있는 유저들은 그대로 두고 종료. GameServer 종료 흐름에서 별도 처리됨.
    m_users.clear();
}

void Stage::EnqueueMessage(StageMessage msg)
{
    std::lock_guard<std::mutex> lock(m_pendingMessagesMutex);
    m_pendingMessages.push_back(std::move(msg));
}

Monster* Stage::SpawnMonster(int32 monsterKey, float posX, float posY, float posZ, float yaw)
{
    const GameData_Monster* pMonsterData = GameDataTable_Monster::FindData(monsterKey);
    if (!pMonsterData)
    {
        LOG_WRITE(LogLevel::Error, std::format("GameData_Monster not found. stageId={} monsterKey={}", m_stageId, monsterKey));
        return nullptr;
    }

    GameServer& server = GameServer::Instance();

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
            LOG_WRITE(LogLevel::Error, std::format("spawn pos not on NavMesh, rejected. stageId={} monsterKey={} pos=({},{},{})",
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
        LOG_WRITE(LogLevel::Error, std::format("spawn pos out of world bounds, rejected. stageId={} monsterKey={} pos=({},{},{})",
            m_stageId, monsterKey, spawnX, spawnY, spawnZ));
        return nullptr;
    }

    // ObjectId 발급 후 Monster 생성/초기화. Monster::Initialize 가 종류데이터 기본스탯 적용 + 현재HP/MP 풀피까지 처리한다.
    const int64 objectId = server.GenerateObjectId();
    MonsterPtr spMonster = std::make_shared<Monster>();
    if (!spMonster->Initialize(objectId, pMonsterData))
    {
        LOG_WRITE(LogLevel::Error, std::format("Monster Initialize failed. stageId={} monsterKey={} objectId={}", m_stageId, monsterKey, objectId));
        return nullptr;
    }
    spMonster->SetAI(std::make_unique<MonsterFsmAI>());   // 기본 두뇌: FSM (보스 등은 향후 BT 로 교체)
    spMonster->SetPos(spawnX, spawnY, spawnZ);
    spMonster->SetYaw(yaw);

    // Stage 등록 (통합/타입별 컨테이너 + Stage/업데이트주기/sector).
    // 몬스터 업데이트 주기는 등록 진입점에 명시 전달 (현재 매 tick; 향후 잡몹은 더 긴 주기로).
    registerObject(spMonster, k_monsterUpdateIntervalMs, m_monsterObjects);

    LOG_WRITE(LogLevel::Info, std::format("stageId={} monsterKey={} objectId={} pos=({},{},{}) yaw={} sector=({},{}) totalObjects={}",
        m_stageId, monsterKey, objectId, spawnX, spawnY, spawnZ, yaw,
        spMonster->GetCurSectorX(), spMonster->GetCurSectorZ(), m_objects.size()));

    // 주변 sector AOI 안의 유저들에게 spawn 통보. (몬스터는 관찰자가 아니므로 단방향.)
    const std::vector<GamePacket::MonsterSpawnInfo> singleMonster = { makeMonsterSpawnInfo(*spMonster) };
    ForEachUserInAoi(spMonster->GetCurSectorX(), spMonster->GetCurSectorZ(),
        [&](int64 userId)
        {
            server.GetPacketSender().SendObjectVisibilityNtf(userId, {}, {}, singleMonster);
        });

    return spMonster.get();
}

bool Stage::DespawnMonster(int64 objectId)
{
    auto iter = m_monsterObjects.find(objectId);
    if (iter == m_monsterObjects.end())
    {
        LOG_WRITE(LogLevel::Warn, std::format("monster not found. stageId={} objectId={}", m_stageId, objectId));
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

    LOG_WRITE(LogLevel::Info, std::format("stageId={} objectId={} totalObjects={}", m_stageId, objectId, m_objects.size()));

    // 주변 sector AOI 안의 유저들에게 despawn 통보.
    const std::vector<int64> despawnIds = { objectId };
    ForEachUserInAoi(sx, sz,
        [&](int64 userId)
        {
            GameServer::Instance().GetPacketSender().SendObjectVisibilityNtf(userId, {}, despawnIds);
        });


    return true;
}

// ── sector 등록/제거/이동 헬퍼 ─────────────────────────────
// 섹터는 X-Z 평면으로만 분할됨. Y(높이)는 섹터 분할에 사용 안 함.

void Stage::registerObject(const StageObjectPtr& spObject, int64 updateIntervalMs,
                           std::unordered_map<int64, StageObjectPtr>& typeMap)
{
    const int64 objectId = spObject->GetObjectId();

    // 통합 컨테이너 + 타입별 맵에 등록 (둘 다 같은 shared_ptr 보관).
    m_objects[objectId] = spObject;
    typeMap[objectId]   = spObject;

    spObject->SetStage(this);
    spObject->SetUpdateIntervalMs(updateIntervalMs);   // 필수: 호출자가 지정한 업데이트 주기
    spObject->ResetUpdateClock();                      // 등록 시점부터 주기 카운트 시작

    // 좌표 기준 sector 등록. 맵 범위 밖이면 (-1,-1) 로 남고 addObjectToSector 가 경고 로그를 남긴다.
    addObjectToSector(spObject.get());
}

void Stage::addObjectToSector(StageObject* pObject)
{
    if (!pObject)
        return;

    int32 sectorX = 0;
    int32 sectorZ = 0;
    if (!GetSectorIndex(pObject->GetPosX(), pObject->GetPosZ(), sectorX, sectorZ))
    {
        // 맵 범위 밖. 섹터에 등록 안 함. (-1, -1)로 유지.
        LOG_WRITE(LogLevel::Warn, std::format("object pos out of world bounds. stageId={} objectId={} pos=({},{},{})",
            m_stageId, pObject->GetObjectId(), pObject->GetPosX(), pObject->GetPosY(), pObject->GetPosZ()));
        pObject->SetCurSector(-1, -1);
        return;
    }

    Sector* pSector = GetSector(sectorX, sectorZ);
    if (!pSector)
    {
        LOG_WRITE(LogLevel::Error, std::format("sector not found. stageId={} sector=({},{})", m_stageId, sectorX, sectorZ));
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

    LOG_WRITE(LogLevel::Debug, std::format("stageId={} objectId={} ({},{}) -> ({},{})",
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
                OnUserEnter(m.spUser);
            }
            else if constexpr (std::is_same_v<T, StageMsg_UserLeave>)
            {
                OnUserLeave(m.userId);
            }
        }, msg);
    }
}

void Stage::OnUserEnter(const UserPtr& spUser)
{
    if (!spUser)
        return;

    const int64 userId = spUser->GetUserId();
    m_users[userId] = spUser;
    spUser->SetCurrentStageId(m_stageId);

    // 캐릭터 스폰은 별도 단계 (2단계 입장).
    // 유저가 Moving 상태로 pendingCharacter를 들고 있으면, 클라의 StageLoadCompleteReq
    // 수신 시 spawnPendingCharacter가 스폰한다. SystemStage는 캐릭터가 없으므로 여기서 끝.
    LOG_WRITE(LogLevel::Info, std::format("stageId={} userId={} stageState={} totalUsers={}",
        m_stageId, userId, static_cast<int32>(spUser->GetStageState()), m_users.size()));

    if (m_pScript)
        m_pScript->CallOnPlayerEnter(userId);
}

void Stage::spawnPendingCharacter(const UserPtr& spUser)
{
    if (!spUser)
        return;

    const int64 userId = spUser->GetUserId();

    CharacterPtr spCharacter = spUser->GetCurrentCharacter();
    if (!spCharacter)
    {
        LOG_WRITE(LogLevel::Warn, std::format("no character. stageId={} userId={}", m_stageId, userId));
        return;
    }

    // ── 도착 위치 결정 ────────────────────────────────────
    // 기본값은 캐릭터의 현재 좌표 (positionType == None: 캐릭터 선택 입장 = DB 좌표 복귀).
    // positionType != None 이면 StageStartPosition 데이터 좌표로 덮어쓰고 NavMesh 스냅 보정.
    float posX = spCharacter->GetPosX();
    float posY = spCharacter->GetPosY();
    float posZ = spCharacter->GetPosZ();
    float yaw  = spCharacter->GetYaw();

    const EStagePositionType positionType = spUser->GetPendingPositionType();
    if (positionType != EStagePositionType::None)
    {
        const GameData_StageStartPosition* pPosData =
            GameDataTable_StageStartPosition::FindByStageAndType(GetStageDataKey(), positionType);
        if (pPosData)
        {
            posX = static_cast<float>(pPosData->PosX);
            posY = static_cast<float>(pPosData->PosY);
            posZ = static_cast<float>(pPosData->PosZ);
            yaw  = static_cast<float>(pPosData->Yaw);

            // NavMesh 위로 스냅 (Y 보정 + walkable 보장). 실패 시 데이터 좌표 그대로.
            float snapX = 0.f, snapY = 0.f, snapZ = 0.f;
            if (SampleNavMeshPosition(posX, posY, posZ, 5.f, 5.f, 5.f, snapX, snapY, snapZ))
            {
                posX = snapX; posY = snapY; posZ = snapZ;
            }
        }
        else
        {
            // 이동 요청 시 검증을 통과했으므로 이론상 도달 불가. 캐릭터 현재 좌표로 진행.
            LOG_WRITE(LogLevel::Error, std::format(
                "StageStartPosition not found. stageId={} stageDataKey={} positionType={}",
                m_stageId, GetStageDataKey(), static_cast<int32>(positionType)));
        }
    }

    // 서브클래스가 도착 위치/회전을 보정할 수 있다 (기본 no-op).
    OnResolveSpawnTransform(spCharacter, positionType, posX, posY, posZ, yaw);

    // 위치 배치 + 이동 상태 정지 (이전 Stage의 이동 잔여 상태 제거).
    spCharacter->StopAt(posX, posY, posZ, yaw);

    const int64 objectId = spCharacter->GetObjectId();

    // 캐릭터는 중요 오브젝트 → 매 tick(50ms) 업데이트. 등록 진입점에 주기를 명시 전달.
    registerObject(spCharacter, k_characterUpdateIntervalMs, m_userObjects);

    // 상태 전환 (캐릭터 소유는 이미 User가 갖고 있으므로 별도 설정 불필요).
    spUser->SetStageState(EUserStageState::InStage);

    LOG_WRITE(LogLevel::Info, std::format("stageId={} userId={} characterId={} sector=({},{}) totalUsers={} totalObjects={}",
        m_stageId, userId, objectId,
        spCharacter->GetCurSectorX(), spCharacter->GetCurSectorZ(),
        m_users.size(), m_objects.size()));

    // ── visibility 전파 ────────────────────────────────────
    // 주변 sector AOI 기반.
    GameServer& server = GameServer::Instance();

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
                    server.GetPacketSender().SendObjectVisibilityNtf(otherUserId, singleSpawn, {});
                }
            }

            // 주변 몬스터도 나에게 spawn 통보. (몬스터는 관찰자가 아니므로 받기만 한다.)
            for (const auto& [monsterObjId, pMonsterObj] : pSector->GetMonsters())
            {
                monsterSpawnsForMe.push_back(makeMonsterSpawnInfo(*static_cast<Monster*>(pMonsterObj)));
            }
        });

    // ── StageLoadCompleteRes 를 먼저 보낸다 (StatUpdate/HpMp 는 PacketSender 가 그 뒤에 송신) ──
    // 2단계 입장에서 클라는 이 패킷 수신 시점에 보관 중인 LocalPlayer 를 활성화/배치한다.
    // 그래야 뒤이어 오는 ObjectVisibilityNtf 의 자기 자신 항목을 식별해 스킵할 수 있다.
    // (순서가 반대면 클라가 자기 캐릭터를 원격 캐릭터로 잘못 스폰 → 키 충돌.)
    server.GetPacketSender().SendStageLoadCompleteRes(userId, EResultCode::Success, GetStageId(), GetStageDataKey(),
        spCharacter->GetPosX(), spCharacter->GetPosY(), spCharacter->GetPosZ(), spCharacter->GetYaw());

    server.GetPacketSender().SendObjectVisibilityNtf(userId, spawnsForMe, {}, monsterSpawnsForMe);
    

    // 스폰 완료 후 서브클래스 훅 (기본 no-op). 캐릭터가 Stage에 등록되고 통보까지 끝난 뒤 호출.
    // 예: 입장 버프 부여, 이벤트 트리거.
    OnCharacterSpawned(spUser, spCharacter);
}

void Stage::OnUserLeave(int64 userId)
{
    auto iter = m_users.find(userId);
    if (iter == m_users.end())
    {
        LOG_WRITE(LogLevel::Warn, std::format("user not found. stageId={} userId={}", m_stageId, userId));
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

    LOG_WRITE(LogLevel::Info, std::format("stageId={} userId={} totalUsers={} totalObjects={}",
        m_stageId, userId, m_users.size(), m_objects.size()));

    if (m_pScript)
        m_pScript->CallOnPlayerLeave(userId);

    // 이벤트영역 occupant 에서 제거 — 영역 안에 있던 채로 떠나도 stale 항목이 남지 않게.
    // (남으면 같은 유저 재입장 시 진입 보고가 중복방지에 걸려 콜백이 안 뜸.)
    for (auto& [eventKey, spEventArea] : m_eventAreas)
        spEventArea->RemoveOccupant(userId);

    // 주변 sector의 다른 캐릭터들에게 despawn broadcast.
    if (leavingObjectId != 0)
    {
        std::vector<int64> despawnIds = { leavingObjectId };

        ForEachAdjacentSector(leavingSectorX, leavingSectorZ, k_aoiRange,
            [&](Sector* pSector)
            {
                for (const auto& [otherObjId, pOtherObj] : pSector->GetUsers())
                {
                    Character* pOtherChar = static_cast<Character*>(pOtherObj);
                    const int64 otherUserId = pOtherChar->GetProto().owner_user_id();
                    GameServer::Instance().GetPacketSender().SendObjectVisibilityNtf(otherUserId, {}, despawnIds);
                }
            });
    }
}

void Stage::OnUserPacket(const UserPtr& spUser, const netlib::PacketPtr& spPacket)
{
    if (!spUser || !spPacket)
        return;

    const uint16 packetType = spPacket->GetHeader()->type;
    const UserPacketHandlerMap& handlerMap = getUserPacketHandlerMap();

    auto iter = handlerMap.find(packetType);
    if (iter == handlerMap.end())
    {
        // 등록되지 않은 패킷은 디버그 로그.
        LOG_WRITE(LogLevel::Debug, std::format("unhandled. stageId={} userId={} packetType={} payloadSize={}",
            m_stageId, spUser->GetUserId(),
            packetType, spPacket->GetPayloadSize()));
        return;
    }

    (this->*(iter->second))(spUser, spPacket);
}

// 이 Stage(베이스) 가 처리하는 [패킷ID, 핸들러] 테이블. static const 라 클래스당 1회만 생성된다.
const Stage::UserPacketHandlerMap& Stage::getUserPacketHandlerMap() const
{
    static const UserPacketHandlerMap sm_handlers = {
        { Common::GAME_PACKET_ID_MOVE_INTENT_REQ,          &Stage::handleMoveIntentReq },
        { Common::GAME_PACKET_ID_SKILL_CAST_REQ,           &Stage::handleSkillCastReq },
        { Common::GAME_PACKET_ID_SKILL_PROJECTILE_HIT_REQ, &Stage::handleSkillProjectileHitReq },
        { Common::GAME_PACKET_ID_STAGE_MOVE_REQ,           &Stage::handleStageMoveReq },
        { Common::GAME_PACKET_ID_STAGE_LOAD_COMPLETE_REQ,  &Stage::handleStageLoadCompleteReq },
        { Common::GAME_PACKET_ID_EVENT_AREA_ENTER_REQ,     &Stage::handleEventAreaEnterReq },
        { Common::GAME_PACKET_ID_EVENT_AREA_EXIT_REQ,      &Stage::handleEventAreaExitReq },
        { Common::GAME_PACKET_ID_OBJECT_INTERACT_REQ,      &Stage::handleObjectInteractReq },
#ifdef _DEBUG
        { Common::GAME_PACKET_ID_CHEAT_REQ,                &Stage::handleCheatReq },
#endif
    };
    return sm_handlers;
}

// payload 를 TMsg 로 역직렬화. 실패 시 Warn 로그 + false.
template <typename TMsg>
bool Stage::deserializeUserPacket(const UserPtr& spUser, const netlib::PacketPtr& spPacket, TMsg& outMsg)
{
    if (!GameServer::Instance().DeserializePacket(*spPacket, outMsg))
    {
        LOG_WRITE(LogLevel::Warn, std::format("failed to deserialize. stageId={} userId={} packetType={}",
            m_stageId, spUser->GetUserId(), spPacket->GetHeader()->type));
        return false;
    }

    // [치트] detail 모드: 수신 게임플레이 패킷을 이름+JSON 1줄로 출력.
    // (name 모드는 handleRelayedClientPacket 진입부가 담당하므로 여기선 detail 만.)
    if (packetlog::EffectiveMode(*spUser) == EPacketLogMode::Detail)
        packetlog::LogPacket("C->S", spUser->GetUserId(), spPacket->GetHeader()->type, &outMsg);

    return true;
}

void Stage::handleMoveIntentReq(const UserPtr& spUser, const netlib::PacketPtr& spPacket)
{
    CharacterPtr spCharacter = spUser->GetCurrentCharacter();
    if (!spCharacter || spCharacter->GetStage() != this)
    {
        LOG_WRITE(LogLevel::Warn, std::format("MoveIntentReq but no character or wrong stage. stageId={} userId={}", m_stageId, spUser->GetUserId()));
        return;
    }

    GamePacket::MoveIntentReq req;
    if (!deserializeUserPacket(spUser, spPacket, req))
        return;

    // 화해 기준: 마지막으로 처리한 입력 seq 기록 (SnapshotNtf.ack_input_seq 로 본인에게 송신).
    spCharacter->SetLastInputSeq(req.input_seq());

    // 위치 권위는 서버. 클라는 "의도"만 보낸다(위치 미수신).
    switch (req.intent())
    {
    case GamePacket::MOVE_INTENT_TO:
        // 시전 액션락(제자리시전) 중에는 이동 입력을 무시한다(서버 권위). 잠금 해제 후 다시 받는다.
        if (!spCharacter->IsActionLocked())
            spCharacter->SetDestination(req.dest_x(), req.dest_y(), req.dest_z());
        break;
    case GamePacket::MOVE_INTENT_STOP:
        // 서버 시뮬레이션 현재 위치에서 정지(클라 위치 채택 안 함). yaw 만 클라 hint 반영.
        spCharacter->StopAt(spCharacter->GetPosX(), spCharacter->GetPosY(), spCharacter->GetPosZ(), req.yaw());
        break;
    default:
        break;
    }
    // 이동 복제는 buildAndSendSnapshots 가 매 tick 처리한다.
}

void Stage::handleStageMoveReq(const UserPtr& spUser, const netlib::PacketPtr& spPacket)
{
    GamePacket::StageMoveReq req;
    if (!deserializeUserPacket(spUser, spPacket, req))
        return;

    GameServer& server = GameServer::Instance();

    const int64 userId = spUser->GetUserId();

    auto sendFail = [&](const std::string& reason)
    {
        LOG_WRITE(LogLevel::Warn, std::format("rejected. stageId={} userId={} targetKey={} reason={}",
            m_stageId, userId, req.target_stage_data_key(), reason));
        server.GetPacketSender().SendStageMoveRes(userId, EResultCode::Fail, reason, req.target_stage_data_key());
    };

    // ── 검증 ─────────────────────────────────────────────
    // 크로스서버 이동은 Phase 2.
    if (req.target_game_server_id() != 0 && req.target_game_server_id() != server.GetServerId())
    {
        sendFail("cross-server move not supported yet");
        return;
    }

    // 전환 중 중복 요청 거부.
    if (spUser->GetStageState() != EUserStageState::InStage)
    {
        sendFail("not in stage (moving?)");
        return;
    }

    CharacterPtr spCharacter = spUser->GetCurrentCharacter();
    if (!spCharacter || spCharacter->GetStage() != this)
    {
        sendFail("no character in this stage");
        return;
    }

    const auto positionType = static_cast<EStagePositionType>(req.position_type());
    if (positionType <= EStagePositionType::None || positionType >= EStagePositionType::Max)
    {
        sendFail("invalid position type");
        return;
    }

    // 대상 Stage 해석. 인스턴스 선택 정책 v1: 정적 스테이지는 dataKey당 정확히 1개.
    // (채널/인스턴스 던전 도입 시 이 지점에 선택 정책이 들어간다.)
    std::vector<StagePtr> targets = server.GetStageManager().FindStagesByDataKey(req.target_stage_data_key());
    if (targets.empty())
    {
        sendFail("target stage not found");
        return;
    }
    if (targets.size() > 1)
    {
        LOG_WRITE(LogLevel::Error, std::format("multiple instances for static stage. dataKey={} count={}",
            req.target_stage_data_key(), targets.size()));
        sendFail("ambiguous target stage");
        return;
    }

    StagePtr spTarget = targets[0];
    if (spTarget.get() == this)
    {
        sendFail("already in target stage");
        return;
    }

    const EStageType targetType = spTarget->GetStageType();
    if (targetType != EStageType::Town && targetType != EStageType::Field)
    {
        sendFail("target stage type not movable");
        return;
    }

    // 도착 위치 데이터 존재 검증 (스폰 시점의 lookup 실패를 미리 차단).
    if (!GameDataTable_StageStartPosition::FindByStageAndType(spTarget->GetStageDataKey(), positionType))
    {
        sendFail("no start position data");
        return;
    }

    // ── 이동 확정 ─────────────────────────────────────────
    // 1) 도착 위치 타입만 보관하고 Moving 상태로 전환 (캐릭터 소유는 이미 User가 갖고 있음).
    spUser->SetPendingPositionType(positionType);
    spUser->SetStageState(EUserStageState::Moving);

    // 2) old Stage(=this)에서 퇴장: sector/컨테이너 제거 + AOI despawn + m_users 제거.
    //    캐릭터는 User가 계속 소유하므로 이 시점에 파괴되지 않는다.
    OnUserLeave(userId);

    // 3) target Stage에 유저만 입장 (이후 클라 패킷은 target이 drain).
    spTarget->EnqueueMessage(StageMsg_UserEnter{spUser});

    // 4) 성공 응답 → 클라는 로딩 시작, 완료 시 StageLoadCompleteReq.
    server.GetPacketSender().SendStageMoveRes(userId, EResultCode::Success, "", req.target_stage_data_key());

    LOG_WRITE(LogLevel::Info, std::format("moving. userId={} from stageId={} to stageId={} (dataKey={}) positionType={}",
        userId, m_stageId, spTarget->GetStageId(), req.target_stage_data_key(), static_cast<int32>(positionType)));
}

void Stage::handleStageLoadCompleteReq(const UserPtr& spUser, const netlib::PacketPtr& /*spPacket*/)
{
    // Moving 상태의 유저만 유효 (이외는 잘못된/중복 보고 → 무시).
    if (spUser->GetStageState() != EUserStageState::Moving || !spUser->GetCurrentCharacter())
    {
        LOG_WRITE(LogLevel::Warn, std::format("unexpected. stageId={} userId={} stageState={}",
            m_stageId, spUser->GetUserId(), static_cast<int32>(spUser->GetStageState())));
        return;
    }

    spawnPendingCharacter(spUser);
}

#ifdef _DEBUG
// [치트] 서버치트 요청 처리(개발용). 이 함수는 "패킷 ↔ 치트모듈" 어댑터 역할만 한다.
// 실제 치트 핸들러/테이블은 CheatCommand.cpp 에 모여 있어 Stage 가 치트로 지저분해지지 않는다.
// 새 서버치트 추가는 CheatCommand.cpp 에서만 하면 된다 (이 파일 변경 불필요).
void Stage::handleCheatReq(const UserPtr& spUser, const netlib::PacketPtr& spPacket)
{
    GamePacket::CheatReq req;
    if (!deserializeUserPacket(spUser, spPacket, req))
        return;

    const std::vector<std::string> args(req.args().begin(), req.args().end());
    const CheatResult result = GameServer::Instance().GetCheatManager().Execute(*this, spUser, req.name(), args);

    GamePacket::CheatRes res;
    res.set_success(result.success);
    res.set_message(result.message);
    GameServer::Instance().GetPacketSender().SendToUser(spUser->GetUserId(), Common::GAME_PACKET_ID_CHEAT_RES, res);

    LOG_WRITE(LogLevel::Info, std::format("cheat. stageId={} userId={} name={} success={}",
        m_stageId, spUser->GetUserId(), req.name(), result.success));
}
#endif // _DEBUG

void Stage::handleSkillCastReq(const UserPtr& spUser, const netlib::PacketPtr& spPacket)
{
    CharacterPtr spCharacter = spUser->GetCurrentCharacter();
    if (!spCharacter || spCharacter->GetStage() != this)
    {
        LOG_WRITE(LogLevel::Warn, std::format("SkillCastReq but no character or wrong stage. stageId={} userId={}", m_stageId, spUser->GetUserId()));
        return;
    }

    GamePacket::SkillCastReq req;
    if (!deserializeUserPacket(spUser, spPacket, req))
        return;

    const Vector3 origin(req.origin_x(), req.origin_y(), req.origin_z());
    const Vector3 dir(req.dir_x(), 0.0f, req.dir_z());
    // target_pos: 즉시이동(블링크) 거리 클램프에 사용. target_object_id 는 아직 검증용(미사용).
    const Vector3 targetPos(req.target_pos_x(), 0.0f, req.target_pos_z());
    spCharacter->GetSkillComponent().TryCast(req.skill_key(), origin, dir, targetPos, req.seed());
}

void Stage::handleSkillProjectileHitReq(const UserPtr& spUser, const netlib::PacketPtr& spPacket)
{
    GamePacket::SkillProjectileHitReq req;
    if (!deserializeUserPacket(spUser, spPacket, req))
        return;

    // 한 프레임 분량의 hit 을 한 번에 처리 (배치).
    for (int i = 0; i < req.hits_size(); ++i)
    {
        const GamePacket::SkillHitItem& item = req.hits(i);
        OnSkillProjectileHit(item.effect_id(), item.projectile_index(), item.target_object_id(),
                             item.exploded_at_max_range(), item.exploded_on_terrain(),
                             item.hit_x(), item.hit_z());
    }
}

void Stage::handleEventAreaEnterReq(const UserPtr& spUser, const netlib::PacketPtr& spPacket)
{
    GamePacket::EventAreaEnterReq req;
    if (!deserializeUserPacket(spUser, spPacket, req))
        return;

    const int64 userId   = spUser->GetUserId();
    const int32 eventKey = req.event_key();

    // 1) 유효 영역인지(레이아웃에 정의 + OnStart 에서 생성된 키).
    auto it = m_eventAreas.find(eventKey);
    if (it == m_eventAreas.end())
    {
        LOG_WRITE(LogLevel::Warn, std::format("EventAreaEnterReq unknown key. stageId={} userId={} eventKey={}",
            m_stageId, userId, eventKey));
        return;
    }
    EventArea& area = *it->second;

    // secure 영역은 클라 보고를 무시한다 — 서버가 매 tick 권위 위치로 직접 폴링한다(pollSecureEventAreas).
    if (area.IsSecure())
        return;

    CharacterPtr spCharacter = spUser->GetCurrentCharacter();
    if (!spCharacter || spCharacter->GetStage() != this)
        return;

    // 2) 검증 — 클라는 예측 이동이라 서버의 권위 위치는 latency 만큼 뒤처진다.
    //    권위 위치를 직접 영역과 비교하면 그 lag 만큼 거의 항상 빗나가므로(=mismatch 경고 폭주),
    //    영역 판정은 클라가 보고한 위치로 하고, 권위 위치와의 괴리는 "이동속도×lag시간"으로만 제한한다(거짓 보고 방지).
    const float rx = req.pos_x();
    const float ry = req.pos_y();
    const float rz = req.pos_z();

    constexpr float kReportedBoundaryTol = 0.5f;   // 보고 위치의 경계 지터 허용(m).
    constexpr float kLagSeconds          = 0.5f;   // 권위 위치가 보고보다 뒤처질 수 있는 시간(예측 이동 lag).
    constexpr float kAntiCheatMargin     = 1.0f;   // 위에 더하는 고정 여유(m).

    // (a) 보고 위치가 영역 안인가 — 클라가 감지한 진입점.
    if (!area.Contains(rx, rz, kReportedBoundaryTol))
    {
        LOG_WRITE(LogLevel::Warn, std::format("EventAreaEnterReq reported pos not in area. stageId={} userId={} eventKey={}",
            m_stageId, userId, eventKey));
        return;
    }

    // (b) 보고 위치가 권위 위치에서 "이동속도×lag + 여유" 이내인가 — 멀리 떨어진 영역을 거짓 보고하는 것 방지.
    const float moveSpeed = static_cast<float>(spCharacter->GetStat().Get(EStat::MoveSpdTotal));
    const float maxGap    = moveSpeed * kLagSeconds + kAntiCheatMargin;
    const float gdx = rx - spCharacter->GetPosX();
    const float gdz = rz - spCharacter->GetPosZ();
    if (gdx * gdx + gdz * gdz > maxGap * maxGap)
    {
        LOG_WRITE(LogLevel::Warn, std::format("EventAreaEnterReq reported pos too far from authoritative. stageId={} userId={} eventKey={} maxGap={:.1f}",
            m_stageId, userId, eventKey, maxGap));
        return;
    }

    // 3) 중복 진입 방지 — 이미 안에 있으면 콜백 재발동 안 함(클라 Enter 재전송도 여기서 흡수).
    if (!area.AddOccupant(userId))
        return;

    // 4) 스크립트 트리거. loc 은 영역 안으로 검증된 보고 위치를 넘긴다.
    if (m_pScript)
        m_pScript->CallOnEnterEventArea(eventKey, userId, rx, ry, rz);
}

void Stage::handleEventAreaExitReq(const UserPtr& spUser, const netlib::PacketPtr& spPacket)
{
    GamePacket::EventAreaExitReq req;
    if (!deserializeUserPacket(spUser, spPacket, req))
        return;

    const int64 userId   = spUser->GetUserId();
    const int32 eventKey = req.event_key();

    auto it = m_eventAreas.find(eventKey);
    if (it == m_eventAreas.end())
        return;

    // secure 영역은 클라 보고를 무시한다 — 폴링이 권위(pollSecureEventAreas).
    if (it->second->IsSecure())
        return;

    // 안에 있던 유저만 이탈 처리(중복/허위 이탈 무시). 이탈은 관대하게 — 위치 재검증 생략.
    if (!it->second->RemoveOccupant(userId))
        return;

    CharacterPtr spCharacter = spUser->GetCurrentCharacter();
    const float px = spCharacter ? spCharacter->GetPosX() : req.pos_x();
    const float py = spCharacter ? spCharacter->GetPosY() : req.pos_y();
    const float pz = spCharacter ? spCharacter->GetPosZ() : req.pos_z();

    if (m_pScript)
        m_pScript->CallOnExitEventArea(eventKey, userId, px, py, pz);
}

// secure 영역만 서버가 권위 위치로 직접 폴링한다(클라 보고 미신뢰 — 안티익스플로잇 게이트 등).
// secure 영역은 소수라 영역수×유저수 비용은 작다. 비-secure 영역은 클라 보고로 구동(폴링 안 함).
void Stage::pollSecureEventAreas()
{
    for (auto& [eventKey, spArea] : m_eventAreas)
    {
        EventArea& area = *spArea;
        if (!area.IsSecure())
            continue;

        for (auto& [userId, spUser] : m_users)
        {
            CharacterPtr spCharacter = spUser->GetCurrentCharacter();
            if (!spCharacter || spCharacter->GetStage() != this)
                continue;

            const float px = spCharacter->GetPosX();
            const float pz = spCharacter->GetPosZ();
            const bool inside = area.Contains(px, pz, 0.f);   // 권위 위치 직접 — secure 는 서버가 진실

            if (inside)
            {
                // 신규 진입이면(AddOccupant==true) 콜백.
                if (area.AddOccupant(userId) && m_pScript)
                    m_pScript->CallOnEnterEventArea(eventKey, userId, px, spCharacter->GetPosY(), pz);
            }
            else
            {
                // 안에 있다가 나갔으면(RemoveOccupant==true) 콜백.
                if (area.RemoveOccupant(userId) && m_pScript)
                    m_pScript->CallOnExitEventArea(eventKey, userId, px, spCharacter->GetPosY(), pz);
            }
        }
    }
}

void Stage::handleObjectInteractReq(const UserPtr& spUser, const netlib::PacketPtr& spPacket)
{
    GamePacket::ObjectInteractReq req;
    if (!deserializeUserPacket(spUser, spPacket, req))
        return;

    const int64 userId  = spUser->GetUserId();
    const int32 propKey = req.prop_key();

    // 1) 유효 prop 인지(레이아웃에 정의).
    const StageLayout::Prop* pProp = m_pLayout ? m_pLayout->GetProp(propKey) : nullptr;
    if (!pProp)
    {
        LOG_WRITE(LogLevel::Warn, std::format("ObjectInteractReq unknown prop. stageId={} userId={} propKey={}",
            m_stageId, userId, propKey));
        return;
    }

    CharacterPtr spCharacter = spUser->GetCurrentCharacter();
    if (!spCharacter || spCharacter->GetStage() != this)
        return;

    // 2) 검증 (EventArea 와 동일 결) — 보고 위치가 마커 상호작용 범위 안인지 + 권위 위치와의 괴리 제한.
    const float rx = req.pos_x();
    const float rz = req.pos_z();

    constexpr float kBoundaryTol     = 0.5f;   // 범위 경계 지터 여유(m).
    constexpr float kLagSeconds      = 0.5f;   // 권위 위치가 보고보다 뒤처질 수 있는 시간(예측 이동 lag).
    constexpr float kAntiCheatMargin = 1.0f;

    // (a) 보고 위치가 prop 상호작용 range 안인가(평면).
    const float mdx = rx - pProp->x;
    const float mdz = rz - pProp->z;
    const float reach = pProp->range + kBoundaryTol;
    if (mdx * mdx + mdz * mdz > reach * reach)
    {
        LOG_WRITE(LogLevel::Warn, std::format("ObjectInteractReq out of range. stageId={} userId={} propKey={}",
            m_stageId, userId, propKey));
        return;
    }

    // (b) 보고 위치가 권위 위치에서 "이동속도×lag + 여유" 이내인가(거짓 보고 방지).
    const float moveSpeed = static_cast<float>(spCharacter->GetStat().Get(EStat::MoveSpdTotal));
    const float maxGap    = moveSpeed * kLagSeconds + kAntiCheatMargin;
    const float gdx = rx - spCharacter->GetPosX();
    const float gdz = rz - spCharacter->GetPosZ();
    if (gdx * gdx + gdz * gdz > maxGap * maxGap)
    {
        LOG_WRITE(LogLevel::Warn, std::format("ObjectInteractReq reported pos too far from authoritative. stageId={} userId={} propKey={}",
            m_stageId, userId, propKey));
        return;
    }

    // 3) 스크립트 트리거 (fire-and-forget — prop 상태/Ntf 없음. 연출은 스크립트가 Notice 등으로).
    if (m_pScript)
        m_pScript->CallOnObjectInteract(propKey, userId);
}

void Stage::processUserPackets()
{
    // 각 유저의 패킷 큐를 drain하여 OnUserPacket 호출.
    // 이 루프는 Stage 스레드 전용 접근 구간이므로 m_users에 안전하게 접근 가능.
    //
    // m_users가 아닌 스냅샷을 순회하는 이유: 핸들러(handleStageMoveReq)가
    // OnUserLeave로 m_users를 변경할 수 있어 직접 순회하면 iterator가 무효화된다.
    std::vector<UserPtr> users;
    users.reserve(m_users.size());
    for (auto& [userId, spUser] : m_users)
        users.push_back(spUser);

    std::vector<netlib::PacketPtr> packets;
    for (auto& spUser : users)
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

        // 이 캐릭터의 업데이트 주기에 도달했는지 확인. 아직이면 이번 tick 은 건너뛴다.
        // (캐릭터는 기본 50ms = 매 tick 이라 사실상 건너뛰지 않는다.)
        int64 elapsedMs = 0;
        if (!pCharacter->AdvanceUpdateClock(deltaMs, elapsedMs))
            continue;

        // Update 전 sector 좌표 캐치 (sector 변경 감지용).
        const int32 oldSectorX = pCharacter->GetCurSectorX();
        const int32 oldSectorZ = pCharacter->GetCurSectorZ();

        pCharacter->Update(elapsedMs);

        // 좌표가 바뀌었을 수 있으면 sector 갱신 (도착했거나 이동 중 모두).
        UpdateObjectSector(pCharacter);

        // Buff tick (expire + DoT/HoT). After sector settle so badge broadcast uses current sector.
        pCharacter->GetBuffComponent().Update(elapsedMs);

        // 스킬 체인 진행 (시전 중이면 도래한 페이즈 발동). 이동·sector 갱신 후에 호출해 origin 이 현재 위치를 반영.
        pCharacter->GetSkillComponent().Update(elapsedMs);

        // sector가 바뀐 경우 visibility 갱신.
        const int32 newSectorX = pCharacter->GetCurSectorX();
        const int32 newSectorZ = pCharacter->GetCurSectorZ();
        if (oldSectorX != newSectorX || oldSectorZ != newSectorZ)
        {
            updateVisibilityOnSectorChange(*pCharacter, oldSectorX, oldSectorZ, newSectorX, newSectorZ);
        }

    }
}

// ─────────────────────────────────────────────────────────────
// AOI 스냅샷 스트리밍 (이동 복제의 중심)
// ─────────────────────────────────────────────────────────────
// 각 유저에게 자기 AOI(k_aoiRange) 안의 "이미 보이는" 오브젝트(캐릭터/몬스터)의 현재 권위
// 상태를 SnapshotNtf 로 모아 unicast 한다. spawn/despawn 은 ObjectVisibilityNtf 가 담당하므로
// 여기서는 위치/yaw/flags(transform)만 운반한다.
// 클라: 원격 액터는 스냅샷 사이를 보간, 본인 캐릭터(object_id 일치)는 화해에 사용한다.
// 가변 송신율: 변화 있는 객체만 매 tick, 유휴 객체는 heartbeat 주기로 포함. 헤더는 매 tick 항상 송신.
void Stage::buildAndSendSnapshots()
{
    // pass 1: 오브젝트별로 이번 tick 송신 여부(due)를 한 번 계산한다(여러 관찰자에게 일관).
    //   due = 위치/회전 변화 OR heartbeat. 위치 변화 기준이라 이동·대시·넉백·정지 최종위치가 자동 포함된다.
    for (auto& [objId, spObject] : m_objects)
        spObject->EvaluateSnapshotDue(m_serverTickSeq, k_snapshotIdleHeartbeatTicks);

    // pass 2: 각 유저에게 자기 AOI 안의 due 오브젝트를 모아 송신한다.
    // 헤더(tick_seq/ack)는 due 가 없어도 매 tick 항상 보낸다 — 클라 보간 시계를 굶기지 않기 위함.
    for (auto& [userId, spUser] : m_users)
    {
        Character* pMe = spUser->GetCurrentCharacter().get();
        if (!pMe || pMe->GetStage() != this)
            continue;

        const int32 centerX = pMe->GetCurSectorX();
        const int32 centerZ = pMe->GetCurSectorZ();
        if (centerX < 0 || centerZ < 0)
            continue;   // 섹터 미소속(맵 밖)이면 스킵.

        GamePacket::SnapshotNtf ntf;
        ntf.set_server_tick_seq(m_serverTickSeq);
        ntf.set_ack_input_seq(pMe->GetLastInputSeq());   // 본인 마지막 처리 입력 seq (클라 화해용).

        ForEachAdjacentSector(centerX, centerZ, k_aoiRange,
            [&](Sector* pSector)
            {
                for (const auto& [objId, pObj] : pSector->GetUsers())
                {
                    if (!pObj->IsSnapshotDue())
                        continue;   // 이번 tick 변화 없음(유휴) → 스킵.
                    const Character* pChar = static_cast<const Character*>(pObj);
                    GamePacket::ActorStateInfo* pState = ntf.add_states();
                    pState->set_object_id(pChar->GetObjectId());
                    pState->set_pos_x(pChar->GetPosX());
                    pState->set_pos_y(pChar->GetPosY());
                    pState->set_pos_z(pChar->GetPosZ());
                    pState->set_yaw(pChar->GetYaw());
                    uint32 flags = 0;
                    if (pChar->IsMoving()) flags |= 0x1u;
                    if (pChar->IsDead())   flags |= 0x2u;
                    pState->set_flags(flags);
                }
                for (const auto& [objId, pObj] : pSector->GetMonsters())
                {
                    if (!pObj->IsSnapshotDue())
                        continue;   // 이번 tick 변화 없음(유휴) → 스킵.
                    const Monster* pMon = static_cast<const Monster*>(pObj);
                    GamePacket::ActorStateInfo* pState = ntf.add_states();
                    pState->set_object_id(pMon->GetObjectId());
                    pState->set_pos_x(pMon->GetPosX());
                    pState->set_pos_y(pMon->GetPosY());
                    pState->set_pos_z(pMon->GetPosZ());
                    pState->set_yaw(pMon->GetYaw());
                    uint32 flags = 0;
                    if (pMon->IsMoving()) flags |= 0x1u;
                    if (pMon->IsDead())   flags |= 0x2u;
                    pState->set_flags(flags);
                }
            });

        // due 객체가 있을 때만 전송. 빈 스냅샷 keepalive 는 불필요 — NetClock 이 로컬 시간으로 자체 전진하고,
        // 유휴 객체도 heartbeat 로 ≥1Hz 는 보내므로 클라 시계가 굶지 않는다.
        if (ntf.states_size() > 0)
            GameServer::Instance().GetPacketSender().SendSnapshotNtf(userId, ntf);
    }
}

void Stage::updateVisibilityOnSectorChange(Character& character,
                                           int32 oldSectorX, int32 oldSectorZ,
                                           int32 newSectorX, int32 newSectorZ)
{
    GameServer& server = GameServer::Instance();

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
                server.GetPacketSender().SendObjectVisibilityNtf(otherUserId, singleSpawnOfMe, {});
            }

            // 새로 보이는 sector의 몬스터들도 나에게 spawn. (몬스터는 받기만 한다.)
            for (const auto& [monsterObjId, pMonsterObj] : pSector->GetMonsters())
            {
                newlyVisibleMonstersForMe.push_back(makeMonsterSpawnInfo(*static_cast<Monster*>(pMonsterObj)));
            }
        });

    if (!newlyVisibleSpawnsForMe.empty() || !newlyVisibleMonstersForMe.empty())
    {
        server.GetPacketSender().SendObjectVisibilityNtf(myUserId, newlyVisibleSpawnsForMe, {}, newlyVisibleMonstersForMe);
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
                server.GetPacketSender().SendObjectVisibilityNtf(otherUserId, {}, myDespawnId);
            }

            // 더 이상 안 보이는 sector의 몬스터들은 나에게 despawn (despawn_ids 는 타입 무관).
            for (const auto& [monsterObjId, pMonsterObj] : pSector->GetMonsters())
            {
                despawnIdsForMe.push_back(monsterObjId);
            }
        });

    if (!despawnIdsForMe.empty())
    {
        server.GetPacketSender().SendObjectVisibilityNtf(myUserId, {}, despawnIdsForMe);
    }
}

void Stage::updateMonsterVisibilityOnSectorChange(Monster& monster,
                                                  int32 oldSectorX, int32 oldSectorZ,
                                                  int32 newSectorX, int32 newSectorZ)
{
    GameServer& server = GameServer::Instance();

    const int64 monsterObjectId = monster.GetObjectId();

    // sector (x, z)가 (centerX, centerZ) 기준 k_aoiRange 범위 안에 있는지 검사.
    // centerX/Z 가 -1(맵 밖)이면 어떤 범위에도 속하지 않음.
    auto inAOI = [](int32 x, int32 z, int32 centerX, int32 centerZ) -> bool
    {
        if (centerX < 0 || centerZ < 0)
            return false;
        return std::abs(x - centerX) <= k_aoiRange && std::abs(z - centerZ) <= k_aoiRange;
    };

    // ── newAOI − oldAOI: 새로 이 몬스터를 보게 된 유저들에게 spawn ──
    // (spawn 직후 위치는 다음 tick 의 SnapshotNtf 가 갱신한다. 이동 중이어도 곧 따라온다.)
    const std::vector<GamePacket::MonsterSpawnInfo> singleMonster = { makeMonsterSpawnInfo(monster) };

    ForEachAdjacentSector(newSectorX, newSectorZ, k_aoiRange,
        [&](Sector* pSector)
        {
            // oldAOI 에도 있던 sector 면 이미 보고 있으므로 skip.
            if (inAOI(pSector->GetSectorX(), pSector->GetSectorZ(), oldSectorX, oldSectorZ))
                return;

            for (const auto& [otherObjId, pOtherObj] : pSector->GetUsers())
            {
                Character* pOtherChar = static_cast<Character*>(pOtherObj);
                const int64 otherUserId = pOtherChar->GetProto().owner_user_id();
                server.GetPacketSender().SendObjectVisibilityNtf(otherUserId, {}, {}, singleMonster);
            }
        });

    // ── oldAOI − newAOI: 더 이상 이 몬스터를 못 보는 유저들에게 despawn ──
    const std::vector<int64> despawnId = { monsterObjectId };
    ForEachAdjacentSector(oldSectorX, oldSectorZ, k_aoiRange,
        [&](Sector* pSector)
        {
            // newAOI 에도 있는 sector 면 계속 보이므로 skip.
            if (inAOI(pSector->GetSectorX(), pSector->GetSectorZ(), newSectorX, newSectorZ))
                return;

            for (const auto& [otherObjId, pOtherObj] : pSector->GetUsers())
            {
                Character* pOtherChar = static_cast<Character*>(pOtherObj);
                const int64 otherUserId = pOtherChar->GetProto().owner_user_id();
                server.GetPacketSender().SendObjectVisibilityNtf(otherUserId, {}, despawnId);
            }
        });
}

// Buff badge AOI broadcast (mirrors sendMoveNtfToAoi).
// Called by BuffComponent on add/refresh/stack (BuffNtf) and remove/expire (BuffRemoveNtf).
// Sends to every user in the actor's AOI; the owner is included if they are a user in their own sector.
void Stage::BroadcastBuffNtf(const ActorObject& actor, int32 buffKey, int32 stackCount, int32 remainMs)
{
    GameServer& server = GameServer::Instance();

    const int64 objectId = actor.GetObjectId();
    ForEachUserInAoi(actor.GetCurSectorX(), actor.GetCurSectorZ(),
        [&](int64 userId)
        {
            server.GetPacketSender().SendBuffNtf(userId, objectId, buffKey, stackCount, remainMs);
        });
}

void Stage::BroadcastBuffRemoveNtf(const ActorObject& actor, int32 buffKey)
{
    GameServer& server = GameServer::Instance();

    const int64 objectId = actor.GetObjectId();
    ForEachUserInAoi(actor.GetCurSectorX(), actor.GetCurSectorZ(),
        [&](int64 userId)
        {
            server.GetPacketSender().SendBuffRemoveNtf(userId, objectId, buffKey);
        });
}


