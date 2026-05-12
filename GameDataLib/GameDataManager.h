#pragma once

#include "Generated/Manager/GameDataManagerBase.h"

class GameDataManager : public GameDataManagerBase
{
public:
	GameDataManager() = default;
	virtual ~GameDataManager() = default;

public:
	static bool LoadAllGameData(const std::string& csvPath);

};


