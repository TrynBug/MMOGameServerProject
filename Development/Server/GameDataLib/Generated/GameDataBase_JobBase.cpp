// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include <sstream>
#include <string>
#include <format>

#include "GameDataBase_JobBase.h"
#include "GameData_JobBase.h"

const GameData_JobBase* GameDataTableBase_JobBase::FindData(int64_t key)
{
    auto iter = sm_dataMap.find(key);
    if (iter == sm_dataMap.cend())
        return nullptr;
    return iter->second;
}

bool GameDataTableBase_JobBase::makeGameData(const std::string& line)
{
    std::stringstream ss(line);
    std::string field;
    GameData_JobBase* pData = new GameData_JobBase;

    // 컬럼 순서대로 파싱
    std::getline(ss, field, ','); pData->Key = std::stoll(field);
    std::getline(ss, field, ','); pData->Job = static_cast<EJob>(std::stoi(field));
    std::getline(ss, field, ','); pData->Stat1 = static_cast<EStat>(std::stoi(field));
    std::getline(ss, field, ','); pData->StatValue1 = std::stod(field);
    std::getline(ss, field, ','); pData->Stat2 = static_cast<EStat>(std::stoi(field));
    std::getline(ss, field, ','); pData->StatValue2 = std::stod(field);
    std::getline(ss, field, ','); pData->Stat3 = static_cast<EStat>(std::stoi(field));
    std::getline(ss, field, ','); pData->StatValue3 = std::stod(field);
    std::getline(ss, field, ','); pData->Stat4 = static_cast<EStat>(std::stoi(field));
    std::getline(ss, field, ','); pData->StatValue4 = std::stod(field);
    std::getline(ss, field, ','); pData->Stat5 = static_cast<EStat>(std::stoi(field));
    std::getline(ss, field, ','); pData->StatValue5 = std::stod(field);
    std::getline(ss, field, ','); pData->Stat6 = static_cast<EStat>(std::stoi(field));
    std::getline(ss, field, ','); pData->StatValue6 = std::stod(field);
    std::getline(ss, field, ','); pData->Stat7 = static_cast<EStat>(std::stoi(field));
    std::getline(ss, field, ','); pData->StatValue7 = std::stod(field);
    std::getline(ss, field, ','); pData->Stat8 = static_cast<EStat>(std::stoi(field));
    std::getline(ss, field, ','); pData->StatValue8 = std::stod(field);

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
