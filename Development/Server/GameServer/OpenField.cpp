#include "pch.h"
#include "OpenField.h"

OpenField::OpenField(int64 stageId)
    : OpenField(stageId, LoadStageGridParams(stageId))   // 위임. LoadStageGridParams는 1회만 호출.
{
}

OpenField::OpenField(int64 stageId, const StageGridParams& params)
    : Stage(stageId,
            params.stageType,
            params.worldMinX, params.worldMinY,
            params.worldMaxX, params.worldMaxY,
            params.sectorSize)
{
}

void OpenField::OnStageUpdate(int64 /*deltaMs*/)
{
    // 아직 처리할 게 없음. 향후 유저 업데이트, 몬스터 AI, 시스템 메시지 처리 등이 들어감.
}
