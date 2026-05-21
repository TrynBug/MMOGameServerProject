#include "pch.h"
#include "Stage.h"
#include "Character.h"   // OnUserEnter에서 Character를 m_objects에 등록하기 위해 완전타입 필요
#include "GameServer.h"   // OnUserEnter/OnUserLeave에서 visibility 패킷 전송 언어서 필요

#include "Generated/GameData_Stage.h"

#include <cmath>

namespace
{
    // Character 정보를 CharacterSpawnInfo (패킷) 형식으로 채웁니다.
    GamePacket::CharacterSpawnInfo makeCharacterSpawnInfo(const Character& character)
    {
        const DataStructures::Character& proto = character.GetProto();
        GamePacket::CharacterSpawnInfo info;
        info.set_object_id(character.GetObjectId());
        info.set_owner_user_id(proto.owner_user_id());
        info.set_name(proto.name());
        info.set_job_id(proto.job_id());
        info.set_level(proto.level());
        info.set_hp(proto.hp());
        info.set_max_hp(proto.max_hp());
        info.set_mp(proto.mp());
        info.set_max_mp(proto.max_mp());
        // 좌표는 런타임이 진실의 원천. StageObject에서 가져온다.
        info.set_pos_x(character.GetPosX());
        info.set_pos_y(character.GetPosY());
        info.set_yaw(character.GetYaw());
        return info;
    }

    constexpr int64 k_heartbeatIntervalMs = 5000;   // 5초마다 1번 heartbeat 로그

    // GameData_Stage 로딩 실패 시 fallback grid 값.
    // 데이터가 제대로 로드되지 않더라도 객체 자체는 동작하도록 안전망.
    constexpr double k_fallbackWorldMinX  = -500.0;
    constexpr double k_fallbackWorldMinY  = -500.0;
    constexpr double k_fallbackWorldMaxX  =  500.0;
    constexpr double k_fallbackWorldMaxY  =  500.0;
    constexpr double k_fallbackSectorSize =   50.0;
}

StageGridParams LoadStageGridParams(int64 stageId)
{
    StageGridParams params;
    const GameData_Stage* pData = GameDataTable_Stage::FindData(stageId);
    if (!pData)
    {
        LOG_WRITE(LogLevel::Error, std::format("LoadStageGridParams: GameData_Stage not found. stageId={}. using fallback grid.", stageId));
        params.stageType  = EStageType::None;
        params.worldMinX  = k_fallbackWorldMinX;
        params.worldMinY  = k_fallbackWorldMinY;
        params.worldMaxX  = k_fallbackWorldMaxX;
        params.worldMaxY  = k_fallbackWorldMaxY;
        params.sectorSize = k_fallbackSectorSize;
    }
    else
    {
        params.stageType  = pData->StageType;
        params.worldMinX  = pData->worldMinX;
        params.worldMinY  = pData->worldMinY;
        params.worldMaxX  = pData->worldMaxX;
        params.worldMaxY  = pData->worldMaxY;
        params.sectorSize = pData->sectorSize;
    }
    return params;
}

Stage::Stage(int64 stageId, EStageType stageType,
             double worldMinX, double worldMinY,
             double worldMaxX, double worldMaxY,
             double sectorSize)
    : m_stageId(stageId)
    , m_stageType(stageType)
    , m_worldMinX(worldMinX)
    , m_worldMinY(worldMinY)
    , m_worldMaxX(worldMaxX)
    , m_worldMaxY(worldMaxY)
    , m_sectorSize(sectorSize)
{
    initializeSectorGrid();
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

    if (m_worldMaxX <= m_worldMinX || m_worldMaxY <= m_worldMinY)
    {
        LOG_WRITE(LogLevel::Error, std::format("Stage::initializeSectorGrid - invalid world bounds. stageId={} min=({},{}) max=({},{})",
            m_stageId, m_worldMinX, m_worldMinY, m_worldMaxX, m_worldMaxY));
        return;
    }

    // 섹터 개수 계산 (ceil로 올림하여 맵 영역이 섹터로 빠짐없이 커버되도록).
    const double worldSizeX = m_worldMaxX - m_worldMinX;
    const double worldSizeY = m_worldMaxY - m_worldMinY;
    m_sectorCountX = static_cast<int32>(std::ceil(worldSizeX / m_sectorSize));
    m_sectorCountY = static_cast<int32>(std::ceil(worldSizeY / m_sectorSize));

    if (m_sectorCountX <= 0 || m_sectorCountY <= 0)
    {
        LOG_WRITE(LogLevel::Error, std::format("Stage::initializeSectorGrid - sector count <= 0. stageId={} count=({}x{})",
            m_stageId, m_sectorCountX, m_sectorCountY));
        return;
    }

    // 섹터 그리드 생성 + 각 섹터에 인덱스 설정
    const int32 totalSectors = m_sectorCountX * m_sectorCountY;
    m_sectors.resize(totalSectors);

    for (int32 y = 0; y < m_sectorCountY; ++y)
    {
        for (int32 x = 0; x < m_sectorCountX; ++x)
        {
            m_sectors[sectorIndexToFlat(x, y)].SetIndex(x, y);
        }
    }

    LOG_WRITE(LogLevel::Info, std::format("Stage::initializeSectorGrid - stageId={} world=({},{})~({},{}) sectorSize={} grid={}x{} totalSectors={}",
        m_stageId,
        m_worldMinX, m_worldMinY, m_worldMaxX, m_worldMaxY,
        m_sectorSize, m_sectorCountX, m_sectorCountY, totalSectors));
}

bool Stage::GetSectorIndex(float posX, float posY, int32& outSectorX, int32& outSectorY) const
{
    if (m_sectorSize <= 0.0)
        return false;

    // float와 double 비교: float가 double로 의존적으로 승격. 경계는 정확히 처리됨.
    if (posX < m_worldMinX || posX >= m_worldMaxX || posY < m_worldMinY || posY >= m_worldMaxY)
        return false;

    // 계산은 double로 수행하고 결과만 int32로 캐스팅.
    outSectorX = static_cast<int32>((static_cast<double>(posX) - m_worldMinX) / m_sectorSize);
    outSectorY = static_cast<int32>((static_cast<double>(posY) - m_worldMinY) / m_sectorSize);

    // 부동소수점 오차로 인한 경계값 안전장치
    if (outSectorX >= m_sectorCountX) outSectorX = m_sectorCountX - 1;
    if (outSectorY >= m_sectorCountY) outSectorY = m_sectorCountY - 1;

    return true;
}

bool Stage::IsValidSectorIndex(int32 sectorX, int32 sectorY) const
{
    return sectorX >= 0 && sectorX < m_sectorCountX
        && sectorY >= 0 && sectorY < m_sectorCountY;
}

Sector* Stage::GetSector(int32 sectorX, int32 sectorY)
{
    if (!IsValidSectorIndex(sectorX, sectorY))
        return nullptr;
    return &m_sectors[sectorIndexToFlat(sectorX, sectorY)];
}

const Sector* Stage::GetSector(int32 sectorX, int32 sectorY) const
{
    if (!IsValidSectorIndex(sectorX, sectorY))
        return nullptr;
    return &m_sectors[sectorIndexToFlat(sectorX, sectorY)];
}

Sector* Stage::GetSectorByPos(float posX, float posY)
{
    int32 sectorX = 0;
    int32 sectorY = 0;
    if (!GetSectorIndex(posX, posY, sectorX, sectorY))
        return nullptr;
    return GetSector(sectorX, sectorY);
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

// ── sector 등록/제거/이동 헬퍼 ─────────────────────────────

void Stage::addObjectToSector(StageObject* pObject)
{
    if (!pObject)
        return;

    int32 sectorX = 0;
    int32 sectorY = 0;
    if (!GetSectorIndex(pObject->GetPosX(), pObject->GetPosY(), sectorX, sectorY))
    {
        // 맵 범위 밖. 섭터에 등록 안 함. (-1, -1)로 유지.
        LOG_WRITE(LogLevel::Warn, std::format("Stage::addObjectToSector - object pos out of world bounds. stageId={} objectId={} pos=({},{})",
            m_stageId, pObject->GetObjectId(), pObject->GetPosX(), pObject->GetPosY()));
        pObject->SetCurSector(-1, -1);
        return;
    }

    Sector* pSector = GetSector(sectorX, sectorY);
    if (!pSector)
    {
        LOG_WRITE(LogLevel::Error, std::format("Stage::addObjectToSector - sector not found. stageId={} sector=({},{})",
            m_stageId, sectorX, sectorY));
        pObject->SetCurSector(-1, -1);
        return;
    }

    pSector->AddObject(pObject);
    pObject->SetCurSector(sectorX, sectorY);
}

void Stage::removeObjectFromSector(StageObject* pObject)
{
    if (!pObject)
        return;

    const int32 sectorX = pObject->GetCurSectorX();
    const int32 sectorY = pObject->GetCurSectorY();
    if (sectorX < 0 || sectorY < 0)
        return;   // 섭터에 속한 적 없음.

    Sector* pSector = GetSector(sectorX, sectorY);
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
    int32 newSectorY = 0;
    const bool bInsideWorld = GetSectorIndex(pObject->GetPosX(), pObject->GetPosY(), newSectorX, newSectorY);

    const int32 oldSectorX = pObject->GetCurSectorX();
    const int32 oldSectorY = pObject->GetCurSectorY();

    if (!bInsideWorld)
    {
        // 맵 범위 밖으로 나갔으면 섭터에서 빼고 끝.
        if (oldSectorX >= 0 && oldSectorY >= 0)
        {
            if (Sector* pOldSector = GetSector(oldSectorX, oldSectorY))
                pOldSector->RemoveObject(pObject);
            pObject->SetCurSector(-1, -1);
        }
        return;
    }

    // sector 변경 없으면 no-op.
    if (oldSectorX == newSectorX && oldSectorY == newSectorY)
        return;

    // 이전 sector에서 제거 (있었다면) → 새 sector에 등록.
    if (oldSectorX >= 0 && oldSectorY >= 0)
    {
        if (Sector* pOldSector = GetSector(oldSectorX, oldSectorY))
            pOldSector->RemoveObject(pObject);
    }
    if (Sector* pNewSector = GetSector(newSectorX, newSectorY))
    {
        pNewSector->AddObject(pObject);
    }
    pObject->SetCurSector(newSectorX, newSectorY);

    LOG_WRITE(LogLevel::Debug, std::format("Stage::UpdateObjectSector - stageId={} objectId={} ({},{}) -> ({},{})",
        m_stageId, pObject->GetObjectId(), oldSectorX, oldSectorY, newSectorX, newSectorY));
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
            spCharacter->GetCurSectorX(), spCharacter->GetCurSectorY(),
            m_users.size(), m_objects.size()));

        // ── visibility 전파 ────────────────────────────────────
        // 아직 Sector 도입 전이므로 Stage 전체 범위로 처리 (과도기 동작).
        // 향후 Phase D 이후에 sector AOI 기반으로 전환 예정.
        if (GameServer* pServer = GetGameServer())
        {
            const GamePacket::CharacterSpawnInfo myInfo = makeCharacterSpawnInfo(*spCharacter);

            // 나에게 전송할 spawn 목록 (주변 sector의 모든 캐릭터, 자기 포함).
            std::vector<GamePacket::CharacterSpawnInfo> spawnsForMe;
            spawnsForMe.reserve(16);

            // 다른 캐릭터에게 전송할 용도의 "내 spawn 1개".
            std::vector<GamePacket::CharacterSpawnInfo> singleSpawn = { myInfo };

            ForEachAdjacentSector(spCharacter->GetCurSectorX(), spCharacter->GetCurSectorY(), k_aoiRange,
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
                });

            pServer->SendObjectVisibilityNtf(userId, spawnsForMe, {});
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
    int32 leavingSectorY  = -1;
    UserPtr spUser = iter->second;
    if (spUser)
    {
        CharacterPtr spCharacter = spUser->GetCurrentCharacter();
        if (spCharacter && spCharacter->GetStage() == this)
        {
            leavingObjectId = spCharacter->GetObjectId();

            // 제거 전에 sector 좌표를 캐시 (despawn broadcast 범위 결정용).
            leavingSectorX = spCharacter->GetCurSectorX();
            leavingSectorY = spCharacter->GetCurSectorY();

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

            ForEachAdjacentSector(leavingSectorX, leavingSectorY, k_aoiRange,
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
        spCharacter->SetDestination(req.dest_x(), req.dest_y());
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

        // 클라/서버 위치 오차 검증. 오차 범위 내면 클라 위치 인정, 초과면 서버 위치로 고정.
        const float dx = req.pos_x() - spCharacter->GetPosX();
        const float dy = req.pos_y() - spCharacter->GetPosY();
        const float distSq = dx * dx + dy * dy;
        const float tolSq  = k_movePositionTolerance * k_movePositionTolerance;

        if (distSq <= tolSq)
        {
            spCharacter->StopAt(req.pos_x(), req.pos_y(), req.yaw());
        }
        else
        {
            // 서버 위치로 고정. 향후 위치 보정 패킷 추가 예정.
            LOG_WRITE(LogLevel::Warn, std::format("Stage::OnUserPacket - MoveStopReq position out of tolerance. stageId={} userId={} clientPos=({},{}) serverPos=({},{})",
                m_stageId, spUser->GetUserId(),
                req.pos_x(), req.pos_y(), spCharacter->GetPosX(), spCharacter->GetPosY()));
            spCharacter->StopAt(spCharacter->GetPosX(), spCharacter->GetPosY(), req.yaw());
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
    // 이동 도착시 MoveNtf broadcast, sector 변경시 visibility 갱신은 D-4-b/c에서 추가 예정.
    for (auto& [objectId, spObject] : m_userObjects)
    {
        Character* pCharacter = static_cast<Character*>(spObject.get());

        // Update 전 sector 좌표 캐치 (sector 변경 감지용).
        const int32 oldSectorX = pCharacter->GetCurSectorX();
        const int32 oldSectorY = pCharacter->GetCurSectorY();

        const bool arrived = pCharacter->Update(deltaMs);

        // 좌표가 바뀌었을 수 있으면 sector 갱신 (도착했거나 이동 중 모두).
        UpdateObjectSector(pCharacter);

        // sector가 바뀐 경우 visibility 갱신.
        const int32 newSectorX = pCharacter->GetCurSectorX();
        const int32 newSectorY = pCharacter->GetCurSectorY();
        if (oldSectorX != newSectorX || oldSectorY != newSectorY)
        {
            updateVisibilityOnSectorChange(*pCharacter, oldSectorX, oldSectorY, newSectorX, newSectorY);
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
    const float yaw      = character.GetYaw();
    const float destX    = character.GetDestX();
    const float destY    = character.GetDestY();
    const bool  isMoving = character.IsMoving();

    // 주변 sector의 모든 캐릭터(자기 자신 포함)에게 unicast.
    // 캐릭터의 현재 sector가 -1이면(맵 밖) broadcast 안 함.
    const int32 sx = character.GetCurSectorX();
    const int32 sy = character.GetCurSectorY();
    if (sx < 0 || sy < 0)
        return;

    ForEachAdjacentSector(sx, sy, k_aoiRange,
        [&](Sector* pSector)
        {
            for (const auto& [otherObjId, pOtherObj] : pSector->GetUsers())
            {
                Character* pOtherChar = static_cast<Character*>(pOtherObj);
                const int64 otherUserId = pOtherChar->GetProto().owner_user_id();
                pServer->SendMoveNtf(otherUserId, objectId, posX, posY, yaw, destX, destY, isMoving);
            }
        });
}

void Stage::updateVisibilityOnSectorChange(Character& character,
                                           int32 oldSectorX, int32 oldSectorY,
                                           int32 newSectorX, int32 newSectorY)
{
    GameServer* pServer = GetGameServer();
    if (!pServer)
        return;

    const int64 myObjectId = character.GetObjectId();
    const int64 myUserId   = character.GetProto().owner_user_id();
    const GamePacket::CharacterSpawnInfo myInfo = makeCharacterSpawnInfo(character);

    // sector (x, y)가 (centerX, centerY) 기준 k_aoiRange 범위 안에 있는지 검사.
    // 단, sector 좌표가 -1이면 (맵 밖) 어떤 범위에도 속하지 않음.
    auto inAOI = [](int32 x, int32 y, int32 centerX, int32 centerY) -> bool
    {
        if (centerX < 0 || centerY < 0)
            return false;
        return std::abs(x - centerX) <= k_aoiRange && std::abs(y - centerY) <= k_aoiRange;
    };

    // ── newAOI 순회 ──
    // oldAOI에 없던 sector(=새로 보임)의 캐릭터들에게 spawn 교환.
    // 자기 자신은 제외 (이미 m_userObjects/newSector에 있고, 본인은 spawn 알 필요 없음).
    std::vector<GamePacket::CharacterSpawnInfo> newlyVisibleSpawnsForMe;
    newlyVisibleSpawnsForMe.reserve(8);
    std::vector<GamePacket::CharacterSpawnInfo> singleSpawnOfMe = { myInfo };

    ForEachAdjacentSector(newSectorX, newSectorY, k_aoiRange,
        [&](Sector* pSector)
        {
            // 이 sector가 oldAOI에도 있으면 skip (계속 보이는 영역).
            const int32 sx = pSector->GetSectorX();
            const int32 sy = pSector->GetSectorY();
            if (inAOI(sx, sy, oldSectorX, oldSectorY))
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
        });

    if (!newlyVisibleSpawnsForMe.empty())
    {
        pServer->SendObjectVisibilityNtf(myUserId, newlyVisibleSpawnsForMe, {});
    }

    // ── oldAOI 순회 ──
    // newAOI에 없는 sector(=더 이상 안 보임)의 캐릭터들에게 despawn 교환.
    std::vector<int64> despawnIdsForMe;
    despawnIdsForMe.reserve(8);
    std::vector<int64> myDespawnId = { myObjectId };

    ForEachAdjacentSector(oldSectorX, oldSectorY, k_aoiRange,
        [&](Sector* pSector)
        {
            // 이 sector가 newAOI에도 있으면 skip (계속 보이는 영역).
            const int32 sx = pSector->GetSectorX();
            const int32 sy = pSector->GetSectorY();
            if (inAOI(sx, sy, newSectorX, newSectorY))
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
        });

    if (!despawnIdsForMe.empty())
    {
        pServer->SendObjectVisibilityNtf(myUserId, {}, despawnIdsForMe);
    }
}
