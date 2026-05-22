#include "pch.h"
#include "StageManager.h"
#include "GameServer.h"   // AssignContents/RemoveContents/GetContentsThreadCount 호출

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

    TownPtr spStage = std::make_shared<Town>(stageId);
    spStage->SetGameServer(m_pGameServer);

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
