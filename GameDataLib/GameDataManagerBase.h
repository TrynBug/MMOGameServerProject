#pragma once

// 참고: 이 파일은 자동생성된 파일이 아닙니다.

#include <map>
#include <string>

#include "LoggerLib.h"

class GameDataTable;

class GameDataManagerBase
{
public:
    GameDataManagerBase() = default;
    virtual ~GameDataManagerBase() = default;

public:
    static const std::map<const char*, GameDataTable*>& GetGameDataTables() { return sm_gameDataTables; }

protected:
    static bool createAllGameDataTables();

    template<typename DataTableType>
    static bool createGameDataTable();

protected:
    inline static std::map<const char*, GameDataTable*> sm_gameDataTables;
};


template<typename DataTableType>
bool GameDataManagerBase::createGameDataTable()
{
    DataTableType* pTable = new DataTableType;

    const char* dataName = pTable->GetDataName();
    if (sm_gameDataTables.contains(dataName))
    {
        LOG_WRITE(LogLevel::Error, std::format("Duplicate game data table name. dataName={}", dataName));
        return false;
    }

    sm_gameDataTables.insert(std::pair(dataName, pTable));

    return true;
}