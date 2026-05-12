// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다. 직접 수정하지 마세요.
// =====================================================================

#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <cstdint>

#include "GameDataBase_Monster.h"
#include "GameData_Monster.h"

const GameData_Monster* GameDataTableBase_Monster::FindData(int64_t key)
{
    auto iter = sm_dataMap.find(key);
    if (iter == sm_dataMap.cend())
        return nullptr;

    return iter->second;
}

bool GameDataTableBase_Monster::makeGameData(const std::string& line)
{
    std::stringstream ss(line);
    std::string field;
    GameData_Monster* pData = new GameData_Monster;

    // 컬럼 순서대로 파싱
    std::getline(ss, field, ','); pData->Key = std::stoll(field);
    std::getline(ss, field, ','); pData->HP = std::stoll(field);
    std::getline(ss, field, ','); pData->ItemDropGroup = std::stoll(field);
    std::getline(ss, field, ','); pData->IsItemDrop = StringToBool(field);
    std::getline(ss, field, ','); pData->Exp = std::stod(field);
    std::getline(ss, field, ','); pData->IsExp = StringToBool(field);
    std::getline(ss, field, ','); pData->Grade = static_cast<EMonsterGrade>(std::stoi(field));
    std::getline(ss, field, ','); pData->Stat1 = static_cast<EStat>(std::stoi(field));
    std::getline(ss, field, ','); pData->StatValue1 = std::stod(field);
    std::getline(ss, field, ','); pData->Stat2 = static_cast<EStat>(std::stoi(field));
    std::getline(ss, field, ','); pData->StatValue2 = std::stod(field);
    std::getline(ss, field, ','); pData->Stat3 = static_cast<EStat>(std::stoi(field));
    std::getline(ss, field, ','); pData->StatValue3 = std::stod(field);

    if (pData->Key <= 0)
    {
        LOG_WRITE(LogLevel::Error, std::format("invalid key value. (TableKey = {})", pData->Key));
        return false;
    }

    if (sm_dataMap.contains(pData->Key))
    {
        LOG_WRITE(LogLevel::Error, std::format("Duplicate table key. (TableKey = {})", pData->Key));
        return false;
    }

    if (false == pData->Initialize())
    {
        LOG_WRITE(LogLevel::Error, std::format("Failed to initialize data. (TableKey = {})", pData->Key));
        return false;
    }

    sm_dataMap.insert(std::pair(pData->Key, pData));

    return OnAddData(pData);
}