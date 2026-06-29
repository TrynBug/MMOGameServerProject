// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include <sstream>
#include <string>
#include <format>

#include "GameDataBase_Prop.h"
#include "GameData_Prop.h"

const GameData_Prop* GameDataTableBase_Prop::FindData(int32_t key)
{
    auto iter = sm_dataMap.find(key);
    if (iter == sm_dataMap.cend())
        return nullptr;
    return iter->second;
}

bool GameDataTableBase_Prop::makeGameData(const std::string& line)
{
    std::stringstream ss(line);
    std::string field;
    GameData_Prop* pData = new GameData_Prop;

    // 컬럼 순서대로 파싱
    std::getline(ss, field, ','); pData->Key = std::stoi(field);
    std::getline(ss, field, ','); pData->Interactable = StringToBool(field);
    std::getline(ss, field, ','); pData->StateMode = static_cast<EPropStateMode>(std::stoi(field));
    std::getline(ss, field, ','); pData->StateCount = std::stoi(field);
    std::getline(ss, field, ','); pData->InitialState = std::stoi(field);
    std::getline(ss, field, ','); pData->MaxInteract = std::stoi(field);
    std::getline(ss, field, ','); pData->CooldownMs = std::stoi(field);
    std::getline(ss, field, ','); pData->InteractRange = std::stof(field);
    std::getline(ss, field, ','); pData->DespawnDelayMs = std::stoi(field);
    std::getline(ss, field, ','); pData->Behavior = static_cast<EPropBehavior>(std::stoi(field));
    std::getline(ss, field, ','); pData->PortalStageKey = std::stoi(field);
    std::getline(ss, field, ','); pData->PortalPositionType = static_cast<EStagePositionType>(std::stoi(field));

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
