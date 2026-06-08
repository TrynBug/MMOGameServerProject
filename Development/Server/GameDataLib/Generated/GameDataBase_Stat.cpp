// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include <sstream>
#include <string>
#include <format>

#include "GameDataBase_Stat.h"
#include "GameData_Stat.h"

const GameData_Stat* GameDataTableBase_Stat::FindData(int32_t key)
{
    auto iter = sm_dataMap.find(key);
    if (iter == sm_dataMap.cend())
        return nullptr;
    return iter->second;
}

bool GameDataTableBase_Stat::makeGameData(const std::string& line)
{
    std::stringstream ss(line);
    std::string field;
    GameData_Stat* pData = new GameData_Stat;

    // 컬럼 순서대로 파싱
    std::getline(ss, field, ','); pData->Key = std::stoi(field);
    std::getline(ss, field, ','); pData->Stat = static_cast<EStat>(std::stoi(field));
    std::getline(ss, field, ','); pData->StatGroup = static_cast<EStatGroup>(std::stoi(field));
    std::getline(ss, field, ','); pData->StatOp = static_cast<EStatOp>(std::stoi(field));
    std::getline(ss, field, ','); pData->MinApplyValue = std::stod(field);
    std::getline(ss, field, ','); pData->MaxApplyValue = std::stod(field);

    if (pData->Key <= 0)
    {
        LOG_WRITE(LogLevel::Error, std::format("invalid key value. (TableKey = {})", pData->Key));
        delete pData;
        return false;
    }

    if (sm_dataMap.contains(pData->Key))
    {
        LOG_WRITE(LogLevel::Error, std::format("Duplicate table key. (TableKey = {})", pData->Key));
        delete pData;
        return false;
    }

    if (false == pData->Initialize())
    {
        LOG_WRITE(LogLevel::Error, std::format("Failed to initialize data. (TableKey = {})", pData->Key));
        delete pData;
        return false;
    }

    sm_dataMap.insert(std::pair(pData->Key, pData));

    return OnAddData(pData);
}
