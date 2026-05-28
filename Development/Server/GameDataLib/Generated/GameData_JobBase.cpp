// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 최초 1회만 생성됩니다.
// 이후에는 사용자가 직접 수정할 수 있습니다.
// =====================================================================

#include <format>

#include "LoggerLib.h"
#include "GameData_JobBase.h"


bool GameData_JobBase::Initialize()
{
    // 데이터 1건을 읽은 직후 호출됨

    return true;
}

bool GameDataTable_JobBase::OnAddData(const GameData* pRawData)
{
    const GameData_JobBase* pData = static_cast<const GameData_JobBase*>(pRawData);

    // 데이터가 sm_dataMap 에 추가된 후 호출됨

    return true;
}

bool GameDataTable_JobBase::OnLoadComplete()
{
    // EJob -> 데이터 맵 구성.
    sm_dataByJobMap.clear();

    for (const auto& [key, pData] : sm_dataMap)
    {
        const EJob job = pData->Job;
        if (job == EJob::None)
        {
            LOG_WRITE(LogLevel::Error, std::format("JobBase data has EJob::None. (Key={})", key));
            return false;
        }
        if (sm_dataByJobMap.contains(job))
        {
            LOG_WRITE(LogLevel::Error, std::format("Duplicate EJob in JobBase data. (Job={})", static_cast<int>(job)));
            return false;
        }
        sm_dataByJobMap.insert(std::pair(job, pData));
    }

    return true;
}

const GameData_JobBase* GameDataTable_JobBase::FindDataByJob(EJob job)
{
    auto iter = sm_dataByJobMap.find(job);
    if (iter == sm_dataByJobMap.cend())
        return nullptr;
    return iter->second;
}
