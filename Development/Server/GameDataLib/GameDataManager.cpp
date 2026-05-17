#include "GameDataManager.h"

#include "LoggerLib.h"
#include "GameData.h"

bool GameDataManager::LoadAllGameData(const std::string& csvPath)
{
	if (false == createAllGameDataTables())
		return false;

	for (auto& [dataName, pGameDataTable] : sm_gameDataTables)
	{
		if (false == pGameDataTable->LoadData(csvPath))
		{
			LOG_WRITE(LogLevel::Error, std::format("LoadData failed. dataName={}", dataName));
			return false;
		}
	}

	for (auto& [dataName, pGameDataTable] : sm_gameDataTables)
	{
		if (false == pGameDataTable->OnLoadComplete())
		{
			LOG_WRITE(LogLevel::Error, std::format("OnLoadComplete failed. dataName={}", dataName));
			return false;
		}
	}

	return true;
}
