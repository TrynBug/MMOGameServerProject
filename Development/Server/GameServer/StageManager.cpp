#include "pch.h"
#include "StageManager.h"
#include "GameServer.h"
#include "Stage.h"
#include "Map/NavMeshManager.h"
#include "Generated/GameData_Stage.h"

void StageManager::Initialize(GameServer* pGameServer, int32 contentsThreadCount)
{
    m_pGameServer = pGameServer;
    m_contentsThreadCount = contentsThreadCount;
}

void StageManager::Clear()
{
    // 컨텐츠 스레드에서 제거 + 내부 map 비우기.
    // m_safeStages는 thread-safe map이라 swap 등을 직접 못 하니, ForEach + Erase 패턴 사용.
    // GameServer Shutdown 흐름에서만 호출되므로 다른 스레드 접근 거의 없음.
    std::vector<std::pair<int64, StagePtr>> snapshot;
    m_safeStages.ForEach([&](int64 stageId, const StagePtr& spStage)
    {
        snapshot.emplace_back(stageId, spStage);
    });

    // 제거는 인덱스 먼저 (인덱스에만 있고 m_safeStages에 없는 순간을 만들지 않기 위해).
    {
        std::lock_guard<std::mutex> lock(m_dataKeyIndexMutex);
        m_stageIdsByDataKey.clear();
    }

    for (auto& [stageId, spStage] : snapshot)
    {
        if (m_pGameServer)
        {
            const int32 threadIdx = computeStageThreadIndex(stageId);
            m_pGameServer->RemoveContents(threadIdx, spStage);
        }
        m_safeStages.Erase(stageId);
    }

    m_spSystemStage.reset();
    m_spTown.reset();
}

SystemStagePtr StageManager::CreateSystemStage(int64 stageId, int32 stageDataKey)
{
    if (!m_pGameServer)
    {
        LOG_WRITE(LogLevel::Error, std::format("not initialized. stageId={}", stageId));
        return nullptr;
    }

	const GameData_Stage* pStageData = GameDataTable_Stage::FindData(stageDataKey);
    if (!pStageData)
    {
        LOG_WRITE(LogLevel::Error, std::format("GameData_Stage not found. stageId={}, stageDataKey={}", stageId, stageDataKey));
        return nullptr;
    }

    StagePtr spExisting;
    if (m_safeStages.Find(stageId, spExisting))
    {
        LOG_WRITE(LogLevel::Error, std::format("stageId already exists. stageId={}", stageId));
        return nullptr;
    }

    SystemStagePtr spStage = std::make_shared<SystemStage>(stageId, stageDataKey);
    registerStage(stageId, stageDataKey, spStage);
    m_spSystemStage = spStage;

    LOG_WRITE(LogLevel::Info, std::format("stageId={}", stageId));

    return spStage;
}

TownPtr StageManager::CreateTown(int64 stageId, int32 stageDataKey)
{
    StageGridParams params;
    const dtNavMesh* pNavMesh = nullptr;
    if (!prepareNavStage(stageId, stageDataKey, "CreateTown", params, pNavMesh))
        return nullptr;

    TownPtr spStage = std::make_shared<Town>(stageId, stageDataKey, params);
    spStage->SetNavMesh(pNavMesh);
    registerStage(stageId, stageDataKey, spStage);
    m_spTown = spStage;

    LOG_WRITE(LogLevel::Info, std::format("stageId={} stageDataKey={}", stageId, stageDataKey));

    return spStage;
}

FieldPtr StageManager::CreateField(int64 stageId, int32 stageDataKey)
{
    StageGridParams params;
    const dtNavMesh* pNavMesh = nullptr;
    if (!prepareNavStage(stageId, stageDataKey, "CreateField", params, pNavMesh))
        return nullptr;

    FieldPtr spStage = std::make_shared<Field>(stageId, stageDataKey, params);
    spStage->SetNavMesh(pNavMesh);
    registerStage(stageId, stageDataKey, spStage);

    LOG_WRITE(LogLevel::Info, std::format("stageId={} stageDataKey={}", stageId, stageDataKey));

    return spStage;
}

bool StageManager::prepareNavStage(int64 stageId, int32 stageDataKey, const char* logTag, StageGridParams& outParams, const dtNavMesh*& outNavMesh)
{
    if (!m_pGameServer)
    {
        LOG_WRITE(LogLevel::Error, std::format("{} - not initialized. stageId={}", logTag, stageId));
        return false;
    }

    const GameData_Stage* pStageData = GameDataTable_Stage::FindData(stageDataKey);
    if (!pStageData)
    {
        LOG_WRITE(LogLevel::Error, std::format("{} - GameData_Stage not found. stageId={}, stageDataKey={}", logTag, stageId, stageDataKey));
        return false;
    }

    StagePtr spExisting;
    if (m_safeStages.Find(stageId, spExisting))
    {
        LOG_WRITE(LogLevel::Error, std::format("{} - stageId already exists. stageId={}", logTag, stageId));
        return false;
    }

    // 1) GameData_Stage 에서 stageType/navMeshFileName/sectorSize 읽는다. worldMin/Max 는 fallback.
    outParams = LoadStageGridParams(stageDataKey);

    // 2) NavMesh 메타가 있으면 worldMin/Max 를 해당 메타의 bounds 로 덮어쓴다.
    //    메타가 없으면(파일 누락 등) fallback 그대로 사용.
    outNavMesh = nullptr;
    const NavMeshMeta* pMeta = nullptr;
    if (!outParams.navMeshFileName.empty())
    {
        outNavMesh = m_pGameServer->GetNavMeshManager().Find(outParams.navMeshFileName);
        pMeta      = m_pGameServer->GetNavMeshManager().FindMeta(outParams.navMeshFileName);
    }

    if (pMeta)
    {
        outParams.worldMinX = pMeta->minX;
        outParams.worldMinZ = pMeta->minZ;
        outParams.worldMaxX = pMeta->maxX;
        outParams.worldMaxZ = pMeta->maxZ;
        LOG_WRITE(LogLevel::Info, std::format(
            "{} - using NavMesh meta bounds. stageId={} navMesh={} bounds=({:.3f},{:.3f})~({:.3f},{:.3f})",
            logTag, stageId, outParams.navMeshFileName,
            outParams.worldMinX, outParams.worldMinZ, outParams.worldMaxX, outParams.worldMaxZ));
    }
    else if (!outParams.navMeshFileName.empty())
    {
        LOG_WRITE(LogLevel::Warn, std::format(
            "{} - NavMesh meta not found, using fallback bounds. stageId={} navMesh={}",
            logTag, stageId, outParams.navMeshFileName));
    }

    if (!outNavMesh && !outParams.navMeshFileName.empty())
    {
        LOG_WRITE(LogLevel::Warn, std::format(
            "{} - NavMesh not found. stageId={} navMesh={} (길찾기 비활성화)",
            logTag, stageId, outParams.navMeshFileName));
    }

    return true;
}

void StageManager::registerStage(int64 stageId, int32 stageDataKey, const StagePtr& spStage)
{
    const int32 threadIdx = computeStageThreadIndex(stageId);
    m_pGameServer->AssignContents(threadIdx, spStage);

    // 등록은 m_safeStages 먼저, 인덱스 나중 (조회 측은 인덱스 miss만 발생).
    m_safeStages.Insert(stageId, spStage);
    {
        std::lock_guard<std::mutex> lock(m_dataKeyIndexMutex);
        m_stageIdsByDataKey[stageDataKey].push_back(stageId);
    }
}

StagePtr StageManager::Find(int64 stageId) const
{
    StagePtr spStage;
    if (m_safeStages.Find(stageId, spStage))
        return spStage;
    return nullptr;
}

std::vector<StagePtr> StageManager::FindStagesByDataKey(int32 stageDataKey) const
{
    std::vector<int64> stageIds;
    {
        std::lock_guard<std::mutex> lock(m_dataKeyIndexMutex);
        auto it = m_stageIdsByDataKey.find(stageDataKey);
        if (it != m_stageIdsByDataKey.end())
            stageIds = it->second;
    }

    std::vector<StagePtr> stages;
    stages.reserve(stageIds.size());
    for (int64 stageId : stageIds)
    {
        StagePtr spStage;
        if (m_safeStages.Find(stageId, spStage))
            stages.push_back(std::move(spStage));
    }
    return stages;
}

int32 StageManager::computeStageThreadIndex(int64 stageId) const
{
    if (m_contentsThreadCount <= 0)
        return 0;
    return static_cast<int32>(stageId % m_contentsThreadCount);
}
