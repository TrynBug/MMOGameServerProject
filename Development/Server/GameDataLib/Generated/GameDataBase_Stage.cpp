// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include <sstream>
#include <string>
#include <format>

#include "GameDataBase_Stage.h"
#include "GameData_Stage.h"

const GameData_Stage* GameDataTableBase_Stage::FindData(int32_t key)
{
    auto iter = sm_dataMap.find(key);
    if (iter == sm_dataMap.cend())
        return nullptr;
    return iter->second;
}

bool GameDataTableBase_Stage::makeGameData(const std::string& line)
{
    std::stringstream ss(line);
    std::string field;
    GameData_Stage* pData = new GameData_Stage;

    // 컬럼 순서대로 파싱
    std::getline(ss, field, ','); pData->Key = std::stoi(field);
    std::getline(ss, field, ','); pData->StageType = static_cast<EStageType>(std::stoi(field));
    std::getline(ss, field, ','); pData->NavMeshFileName = field;
    std::getline(ss, field, ','); pData->StageLayoutFileName = field;
    std::getline(ss, field, ','); pData->sectorSize = std::stof(field);
    std::getline(ss, field, ','); pData->ScriptName1 = field;
    std::getline(ss, field, ','); pData->ScriptName2 = field;
    std::getline(ss, field, ','); pData->ScriptName3 = field;
    std::getline(ss, field, ','); pData->ChannelCount = std::stoi(field);
    std::getline(ss, field, ','); pData->ChannelSoftCap = std::stoi(field);

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
