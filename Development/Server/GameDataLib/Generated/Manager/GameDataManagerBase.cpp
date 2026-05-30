// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다. 직접 수정하지 마세요.
// =====================================================================

#include "../GameDataManagerBase.h"

#include "../GameData_JobBase.h"
#include "../GameData_Monster.h"
#include "../GameData_Skill.h"
#include "../GameData_Stage.h"
#include "../GameData_Stat.h"

bool GameDataManagerBase::createAllGameDataTables()
{
	if (!createGameDataTable<GameDataTable_JobBase>()) return false;
	if (!createGameDataTable<GameDataTable_Monster>()) return false;
	if (!createGameDataTable<GameDataTable_Skill>()) return false;
	if (!createGameDataTable<GameDataTable_Stage>()) return false;
	if (!createGameDataTable<GameDataTable_Stat>()) return false;

	return true;
}
