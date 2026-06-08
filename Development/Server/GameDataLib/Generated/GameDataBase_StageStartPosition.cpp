// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include <sstream>
#include <string>
#include <format>

#include "GameDataBase_StageStartPosition.h"
#include "GameData_StageStartPosition.h"

const GameData_StageStartPosition* GameDataTableBase_StageStartPosition::FindData(int64_t key)
{
    auto iter = sm_dataMap.find(key);
    if (iter == sm_dataMap.cend())
        return nullptr;
    return iter->second;
}

bool GameDataTableBase_StageStartPosition::makeGameData(const std::string& line)
{
    std::stringstream ss(line);
    std::string field;
    GameData_StageStartPosition* pData = new GameData_StageStartPosition;

    // 컬럼 순서대로 파싱
    std::getline(ss, field, ','); pData->Key = std::stoll(field);
    std::getline(ss, field, ','); pData->StageKey = std::stoll(field);
    std::getline(ss, field, ','); pData->StagePositionType = static_cast<EStagePositionType>(std::stoi(field));
    std::getline(ss, field, ','); pData->PosX = std::stod(field);
    std::getline(ss, field, ','); pData->PosY = std::stod(field);
    std::getline(ss, field, ','); pData->PosZ = std::stod(field);
    std::getline(ss, field, ','); pData->Yaw = std::stod(field);

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
