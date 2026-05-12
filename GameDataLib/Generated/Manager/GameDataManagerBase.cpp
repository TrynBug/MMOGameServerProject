// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다. 직접 수정하지 마세요.
// =====================================================================

#include "GameDataManagerBase.h"

#include "../GameData_Monster.h"

bool GameDataManagerBase::createAllGameDataTables()
{
	if(!createGameDataTable<GameDataTable_Monster>()) return false;

	return true;
}
