// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 최초 1회만 생성됩니다.
// 이후에는 사용자가 직접 수정할 수 있습니다.
// =====================================================================

#include "LoggerLib.h"
#include "GameData_Item.h"

#include <format>


bool GameData_Item::Initialize()
{
    if (MaxStack <= 0)
    {
        LOG_WRITE(LogLevel::Error, std::format("Item MaxStack must be positive. itemKey={} maxStack={}", Key, MaxStack));
        return false;
    }
    return ItemType >= 0 && Grade >= 0;
}

bool GameDataTable_Item::OnAddData(const GameData* pRawData)
{
    static_cast<void>(pRawData);
    return true;
}

bool GameDataTable_Item::OnLoadComplete()
{
    return true;
}
