#include "pch.h"
#include "Stages/Town.h"

Town::Town(int64 stageId, int32 stageDataKey)
    : Town(stageId, stageDataKey, LoadStageGridParams(stageDataKey))   // 위임. LoadStageGridParams는 1회만 호출.
{
}

Town::Town(int64 stageId, int32 stageDataKey, const StageGridParams& params)
    : Stage(stageId, stageDataKey,
            params.stageType,
            params.worldMinX, params.worldMinZ,
            params.worldMaxX, params.worldMaxZ,
            params.sectorSize)
{
}

void Town::OnStageUpdate(int64 /*deltaMs*/)
{
    // 마을 Stage. 현재는 처리할 게 없음.
    // 향후 NPC 업데이트, 마을 이벤트 등이 들어갈 예정.
}

void Town::OnStart()
{
    // 부모 처리 (로그 + 배치데이터/스포너 초기화).
    // 몬스터 스폰은 이제 데이터 구동(StageLayout + MonsterSpawner)으로 처리된다.
    Stage::OnStart();
}

// 유저 입장/캐릭터 스폰은 Stage 베이스의 2단계 입장 흐름이 처리한다
// (OnUserEnter → 클라 StageLoadCompleteReq → spawnPendingCharacter → StageLoadCompleteRes).
