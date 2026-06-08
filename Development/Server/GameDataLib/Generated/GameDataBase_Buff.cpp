// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include <sstream>
#include <string>
#include <format>

#include "GameDataBase_Buff.h"
#include "GameData_Buff.h"

const GameData_Buff* GameDataTableBase_Buff::FindData(int32_t key)
{
    auto iter = sm_dataMap.find(key);
    if (iter == sm_dataMap.cend())
        return nullptr;
    return iter->second;
}

bool GameDataTableBase_Buff::makeGameData(const std::string& line)
{
    std::stringstream ss(line);
    std::string field;
    GameData_Buff* pData = new GameData_Buff;

    // 컬럼 순서대로 파싱
    std::getline(ss, field, ','); pData->Key = std::stoi(field);
    std::getline(ss, field, ','); pData->Name = field;
    std::getline(ss, field, ','); pData->Desc = field;
    std::getline(ss, field, ','); pData->Category = static_cast<EBuffCategory>(std::stoi(field));
    std::getline(ss, field, ','); pData->DurationMs = std::stoi(field);
    std::getline(ss, field, ','); pData->MaxStack = std::stoi(field);
    std::getline(ss, field, ','); pData->StackPolicy = static_cast<EBuffStackPolicy>(std::stoi(field));
    std::getline(ss, field, ','); pData->Dispellable = StringToBool(field);
    std::getline(ss, field, ','); pData->RemoveOnDeath = StringToBool(field);
    std::getline(ss, field, ','); pData->RemoveOnStageChange = StringToBool(field);
    std::getline(ss, field, ','); pData->TickIntervalMs = std::stoi(field);
    std::getline(ss, field, ','); pData->PeriodicType = static_cast<EPeriodicEffect>(std::stoi(field));
    std::getline(ss, field, ','); pData->PeriodicValue = std::stod(field);
    std::getline(ss, field, ','); pData->Stat1 = static_cast<EStat>(std::stoi(field));
    std::getline(ss, field, ','); pData->StatValue1 = std::stod(field);
    std::getline(ss, field, ','); pData->Stat2 = static_cast<EStat>(std::stoi(field));
    std::getline(ss, field, ','); pData->StatValue2 = std::stod(field);
    std::getline(ss, field, ','); pData->Stat3 = static_cast<EStat>(std::stoi(field));
    std::getline(ss, field, ','); pData->StatValue3 = std::stod(field);

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
