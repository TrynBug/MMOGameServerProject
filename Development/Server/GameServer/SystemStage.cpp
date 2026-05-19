#include "pch.h"
#include "SystemStage.h"

SystemStage::SystemStage(int64 stageId)
    : SystemStage(stageId, LoadStageGridParams(stageId))   // 위임. LoadStageGridParams는 1회만 호출.
{
}

SystemStage::SystemStage(int64 stageId, const StageGridParams& params)
    : Stage(stageId,
            params.stageType,
            params.worldMinX, params.worldMinY,
            params.worldMaxX, params.worldMaxY,
            params.sectorSize)
{
}

void SystemStage::OnStageUpdate(int64 /*deltaMs*/)
{
    // 캐릭터 선택창 Stage. 현재는 처리할 게 없음.
    // 향후 캐릭터 선택 패킷 핸들러 등이 들어갈 예정.
}
