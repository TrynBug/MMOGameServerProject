#include "pch.h"
#include "StageManager.h"
#include "GameServer.h"   // AssignContents/RemoveContents/GetContentsThreadCount 호출
#include "Stage.h"        // LoadStageGridParams
#include "Map/NavMeshManager.h"  // NavMeshMeta

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

SystemStagePtr StageManager::CreateSystemStage(int64 stageId)
{
    if (!m_pGameServer)
    {
        LOG_WRITE(LogLevel::Error, std::format("StageManager::CreateSystemStage - not initialized. stageId={}", stageId));
        return nullptr;
    }

    StagePtr spExisting;
    if (m_safeStages.Find(stageId, spExisting))
    {
        LOG_WRITE(LogLevel::Error, std::format("StageManager::CreateSystemStage - stageId already exists. stageId={}", stageId));
        return nullptr;
    }

    SystemStagePtr spStage = std::make_shared<SystemStage>(stageId);
    spStage->SetGameServer(m_pGameServer);

    const int32 threadIdx = computeStageThreadIndex(stageId);
    m_pGameServer->AssignContents(threadIdx, spStage);

    m_safeStages.Insert(stageId, spStage);
    m_spSystemStage = spStage;

    LOG_WRITE(LogLevel::Info, std::format("StageManager::CreateSystemStage - stageId={} assignedThreadIndex={}",
        stageId, threadIdx));

    return spStage;
}

TownPtr StageManager::CreateTown(int64 stageId)
{
    if (!m_pGameServer)
    {
        LOG_WRITE(LogLevel::Error, std::format("StageManager::CreateTown - not initialized. stageId={}", stageId));
        return nullptr;
    }

    StagePtr spExisting;
    if (m_safeStages.Find(stageId, spExisting))
    {
        LOG_WRITE(LogLevel::Error, std::format("StageManager::CreateTown - stageId already exists. stageId={}", stageId));
        return nullptr;
    }

    // 1) GameData_Stage 에서 stageType/navMeshFileName/sectorSize 읽는다. worldMin/Max 는 fallback.
    StageGridParams params = LoadStageGridParams(stageId);

    // 2) NavMesh 메타가 있으면 worldMin/Max 를 해당 메타의 bounds 로 덮어쓴다.
    //    메타가 없으면(파일 누락 등) fallback 그대로 사용.
    const dtNavMesh*   pNavMesh = nullptr;
    const NavMeshMeta* pMeta    = nullptr;
    if (!params.navMeshFileName.empty())
    {
        pNavMesh = m_pGameServer->GetNavMeshManager().Find(params.navMeshFileName);
        pMeta    = m_pGameServer->GetNavMeshManager().FindMeta(params.navMeshFileName);
    }

    if (pMeta)
    {
        params.worldMinX = pMeta->minX;
        params.worldMinZ = pMeta->minZ;
        params.worldMaxX = pMeta->maxX;
        params.worldMaxZ = pMeta->maxZ;
        LOG_WRITE(LogLevel::Info, std::format(
            "StageManager::CreateTown - using NavMesh meta bounds. stageId={} navMesh={} bounds=({:.3f},{:.3f})~({:.3f},{:.3f})",
            stageId, params.navMeshFileName,
            params.worldMinX, params.worldMinZ, params.worldMaxX, params.worldMaxZ));
    }
    else if (!params.navMeshFileName.empty())
    {
        LOG_WRITE(LogLevel::Warn, std::format(
            "StageManager::CreateTown - NavMesh meta not found, using fallback bounds. stageId={} navMesh={}",
            stageId, params.navMeshFileName));
    }

    // 3) Town 생성. 명시적 params 를 전달하는 생성자 사용.
    TownPtr spStage = std::make_shared<Town>(stageId, params);
    spStage->SetGameServer(m_pGameServer);

    // 4) NavMesh 객체 부착. nullptr 이면 길찾기 비활성화.
    if (!pNavMesh && !params.navMeshFileName.empty())
    {
        LOG_WRITE(LogLevel::Warn, std::format(
            "StageManager::CreateTown - NavMesh not found. stageId={} navMesh={} (길찾기 비활성화)",
            stageId, params.navMeshFileName));
    }
    spStage->SetNavMesh(pNavMesh);

    const int32 threadIdx = computeStageThreadIndex(stageId);
    m_pGameServer->AssignContents(threadIdx, spStage);

    m_safeStages.Insert(stageId, spStage);
    m_spTown = spStage;

    LOG_WRITE(LogLevel::Info, std::format("StageManager::CreateTown - stageId={} assignedThreadIndex={}",
        stageId, threadIdx));

    return spStage;
}

StagePtr StageManager::Find(int64 stageId) const
{
    StagePtr spStage;
    if (m_safeStages.Find(stageId, spStage))
        return spStage;
    return nullptr;
}

int32 StageManager::computeStageThreadIndex(int64 stageId) const
{
    if (m_contentsThreadCount <= 0)
        return 0;
    return static_cast<int32>(stageId % m_contentsThreadCount);
}
