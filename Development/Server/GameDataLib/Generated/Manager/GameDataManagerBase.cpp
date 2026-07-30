// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다. 직접 수정하지 마세요.
// =====================================================================

#include "../GameDataManagerBase.h"

#include "../GameData_Buff.h"
#include "../GameData_Item.h"
#include "../GameData_ItemDrop.h"
#include "../GameData_JobBase.h"
#include "../GameData_Monster.h"
#include "../GameData_MonsterAI.h"
#include "../GameData_Prop.h"
#include "../GameData_Skill.h"
#include "../GameData_Spawner.h"
#include "../GameData_SpawnGroup.h"
#include "../GameData_Stage.h"
#include "../GameData_StageStartPosition.h"
#include "../GameData_Stat.h"

bool GameDataManagerBase::createAllGameDataTables()
{
	if (!createGameDataTable<GameDataTable_Buff>()) return false;
	if (!createGameDataTable<GameDataTable_Item>()) return false;
	if (!createGameDataTable<GameDataTable_ItemDrop>()) return false;
	if (!createGameDataTable<GameDataTable_JobBase>()) return false;
	if (!createGameDataTable<GameDataTable_Monster>()) return false;
	if (!createGameDataTable<GameDataTable_MonsterAI>()) return false;
	if (!createGameDataTable<GameDataTable_Prop>()) return false;
	if (!createGameDataTable<GameDataTable_Skill>()) return false;
	if (!createGameDataTable<GameDataTable_Spawner>()) return false;
	if (!createGameDataTable<GameDataTable_SpawnGroup>()) return false;
	if (!createGameDataTable<GameDataTable_Stage>()) return false;
	if (!createGameDataTable<GameDataTable_StageStartPosition>()) return false;
	if (!createGameDataTable<GameDataTable_Stat>()) return false;

	return true;
}
