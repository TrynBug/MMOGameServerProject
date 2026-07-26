// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include <sstream>
#include <string>
#include <format>

#include "GameDataBase_MonsterAI.h"
#include "GameData_MonsterAI.h"

const GameData_MonsterAI* GameDataTableBase_MonsterAI::FindData(int32_t key)
{
    auto iter = sm_dataMap.find(key);
    if (iter == sm_dataMap.cend())
        return nullptr;
    return iter->second;
}

bool GameDataTableBase_MonsterAI::makeGameData(const std::string& line)
{
    std::stringstream ss(line);
    std::string field;
    GameData_MonsterAI* pData = new GameData_MonsterAI;

    // 컬럼 순서대로 파싱
    std::getline(ss, field, ','); pData->Key = std::stoi(field);
    std::getline(ss, field, ','); pData->Name = field;
    std::getline(ss, field, ','); pData->AIType = static_cast<EMonsterAIType>(std::stoi(field));
    std::getline(ss, field, ','); pData->AggroRange = std::stof(field);
    std::getline(ss, field, ','); pData->LeashRange = std::stof(field);
    std::getline(ss, field, ','); pData->DesiredRange = std::stof(field);
    std::getline(ss, field, ','); pData->EngagedUpdateIntervalMs = std::stoi(field);
    std::getline(ss, field, ','); pData->WanderRadius = std::stof(field);
    std::getline(ss, field, ','); pData->WanderMinIntervalMs = std::stoi(field);
    std::getline(ss, field, ','); pData->WanderMaxIntervalMs = std::stoi(field);
    std::getline(ss, field, ','); pData->IgnoreSkillRange = StringToBool(field);

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
