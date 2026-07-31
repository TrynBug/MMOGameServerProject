// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 최초 1회만 생성됩니다.
// 이후에는 사용자가 직접 수정할 수 있습니다.
// =====================================================================

#include "LoggerLib.h"
#include "GameData_ItemDrop.h"
#include "GameData_Item.h"

#include <format>


bool GameData_ItemDrop::Initialize()
{
    if (GroupKey <= 0 || ChancePermyriad < 0 || ChancePermyriad > 10000 || MinCount <= 0 || MaxCount < MinCount)
    {
        LOG_WRITE(LogLevel::Error, std::format(
            "invalid ItemDrop row. key={} group={} chance={} count={}~{}",
            Key, GroupKey, ChancePermyriad, MinCount, MaxCount));
        return false;
    }

    return true;
}

bool GameDataTable_ItemDrop::OnAddData(const GameData* pRawData)
{
    static_cast<void>(pRawData);
    return true;
}

bool GameDataTable_ItemDrop::OnLoadComplete()
{
    for (const auto& [key, pData] : sm_dataMap)
    {
        const GameData_Item* pItem = GameDataTable_Item::FindData(pData->ItemKey);
        if (pItem == nullptr || pData->MaxCount > pItem->MaxStack)
        {
            LOG_WRITE(LogLevel::Error, std::format(
                "invalid ItemDrop item reference/count. key={} itemKey={} maxCount={}",
                pData->Key, pData->ItemKey, pData->MaxCount));
            return false;
        }
    }

    return true;
}
