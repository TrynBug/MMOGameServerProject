#include "pch.h"
#include "Town.h"

Town::Town(int64 stageId)
    : Town(stageId, LoadStageGridParams(stageId))   // 위임. LoadStageGridParams는 1회만 호출.
{
}

Town::Town(int64 stageId, const StageGridParams& params)
    : Stage(stageId,
            params.stageType,
            params.worldMinX, params.worldMinY,
            params.worldMaxX, params.worldMaxY,
            params.sectorSize)
{
}

void Town::OnStageUpdate(int64 /*deltaMs*/)
{
    // 마을 Stage. 현재는 처리할 게 없음.
    // 향후 NPC 업데이트, 마을 이벤트 등이 들어갈 예정.
}
