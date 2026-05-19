#include "pch.h"
#include "Stage.h"

#include "Generated/GameData_Stage.h"

#include <cmath>

namespace
{
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

    // 3. 파생 클래스 로직
    OnStageUpdate(deltaMs);

    // 4. heartbeat 로그 (5초마다 1번)
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

    LOG_WRITE(LogLevel::Info, std::format("Stage::OnUserEnter - stageId={} userId={} totalUsers={}",
        m_stageId, userId, m_users.size()));
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

    m_users.erase(iter);

    LOG_WRITE(LogLevel::Info, std::format("Stage::OnUserLeave - stageId={} userId={} totalUsers={}",
        m_stageId, userId, m_users.size()));
}

void Stage::OnUserPacket(const UserPtr& spUser, const netlib::PacketPtr& spPacket)
{
    // 기본 동작: 로그만 출력. 향후 단계에서 실제 디스패쳐 호출 등을 추가한다.
    LOG_WRITE(LogLevel::Debug, std::format("Stage::OnUserPacket - stageId={} userId={} packetType={} payloadSize={}",
        m_stageId, spUser->GetUserId(),
        spPacket->GetHeader()->type, spPacket->GetPayloadSize()));
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
