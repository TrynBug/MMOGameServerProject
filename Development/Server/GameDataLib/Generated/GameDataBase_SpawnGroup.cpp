// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include <sstream>
#include <string>
#include <format>

#include "GameDataBase_SpawnGroup.h"
#include "GameData_SpawnGroup.h"

const GameData_SpawnGroup* GameDataTableBase_SpawnGroup::FindData(int32_t key)
{
    auto iter = sm_dataMap.find(key);
    if (iter == sm_dataMap.cend())
        return nullptr;
    return iter->second;
}

bool GameDataTableBase_SpawnGroup::makeGameData(const std::string& line)
{
    std::stringstream ss(line);
    std::string field;
    GameData_SpawnGroup* pData = new GameData_SpawnGroup;

    // 컬럼 순서대로 파싱
    std::getline(ss, field, ','); pData->Key = std::stoi(field);
    std::getline(ss, field, ','); pData->Name = field;
    std::getline(ss, field, ','); pData->MonsterKey1 = std::stoi(field);
    std::getline(ss, field, ','); pData->MonsterCount1 = std::stoi(field);
    std::getline(ss, field, ','); pData->MonsterKey2 = std::stoi(field);
    std::getline(ss, field, ','); pData->MonsterCount2 = std::stoi(field);
    std::getline(ss, field, ','); pData->MonsterKey3 = std::stoi(field);
    std::getline(ss, field, ','); pData->MonsterCount3 = std::stoi(field);
    std::getline(ss, field, ','); pData->ScatterRadius = std::stof(field);

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
