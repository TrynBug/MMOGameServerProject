#include "pch.h"
#include "OpenField.h"

OpenField::OpenField(int64 stageId)
    : Stage(stageId, StageType::OpenField)
{
}

void OpenField::OnStageUpdate(int64 /*deltaMs*/)
{
    // 2단계: 아직 처리할 게 없음. 향후 유저 업데이트, 몬스터 AI, 시스템 메시지 처리 등이 들어감.
}
