// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include <sstream>
#include <string>
#include <format>

#include "GameDataBase_ItemDrop.h"
#include "GameData_ItemDrop.h"

const GameData_ItemDrop* GameDataTableBase_ItemDrop::FindData(int32_t key)
{
    auto iter = sm_dataMap.find(key);
    if (iter == sm_dataMap.cend())
        return nullptr;
    return iter->second;
}

bool GameDataTableBase_ItemDrop::makeGameData(const std::string& line)
{
    std::stringstream ss(line);
    std::string field;
    GameData_ItemDrop* pData = new GameData_ItemDrop;

    // 컬럼 순서대로 파싱
    std::getline(ss, field, ','); pData->Key = std::stoi(field);
    std::getline(ss, field, ','); pData->GroupKey = std::stoi(field);
    std::getline(ss, field, ','); pData->ItemKey = std::stoi(field);
    std::getline(ss, field, ','); pData->ChancePermyriad = std::stoi(field);
    std::getline(ss, field, ','); pData->MinCount = std::stoi(field);
    std::getline(ss, field, ','); pData->MaxCount = std::stoi(field);

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
