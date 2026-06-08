#include "pch.h"
#include "Field.h"

Field::Field(int64 stageId, int32 stageDataKey)
    : Field(stageId, stageDataKey, LoadStageGridParams(stageDataKey))   // 위임. LoadStageGridParams는 1회만 호출.
{
}

Field::Field(int64 stageId, int32 stageDataKey, const StageGridParams& params)
    : Stage(stageId, stageDataKey,
            params.stageType,
            params.worldMinX, params.worldMinZ,
            params.worldMaxX, params.worldMaxZ,
            params.sectorSize)
{
}

void Field::OnStageUpdate(int64 /*deltaMs*/)
{
    // 필드 Stage. 현재는 처리할 게 없음.
    // 향후 몬스터 스포너, 필드 이벤트 등이 들어갈 예정.
}
