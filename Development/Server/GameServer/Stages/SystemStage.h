#pragma once

#include "pch.h"
#include "Stages/Stage.h"

// 시스템 Stage (캐릭터 선택창)
// 게임서버당 1개 존재. 모든 유저는 게임서버 입장 시 먼저 이 Stage에 들어간다.
// 캐릭터 선택이 완료되면 다른 Stage(Town 등)로 이동한다.
class SystemStage : public Stage
{
public:
    // GameData_Stage 키로 생성. 해당 데이터의 stageType/world bounds/sectorSize를 사용.
    explicit SystemStage(int64 stageId, int32 stageDataKey);
    ~SystemStage() override = default;

    SystemStage(const SystemStage&) = delete;
    SystemStage& operator=(const SystemStage&) = delete;

protected:
    void OnStageUpdate(int64 deltaMs) override;

private:
    // 위임 생성자용 private 생성자. StageGridParams를 1회만 평가하기 위해 사용.
    SystemStage(int64 stageId, int32 stageDataKey, const StageGridParams& params);
};

using SystemStagePtr = std::shared_ptr<SystemStage>;
