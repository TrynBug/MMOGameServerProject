#pragma once

#include "pch.h"
#include "Stage.h"

// 마을 Stage
// 캐릭터 선택이 완료된 유저가 입장하는 기본 Stage.
// 게임서버당 1개 존재.
class Town : public Stage
{
public:
    // GameData_Stage 키로 생성. 해당 데이터의 stageType/world bounds/sectorSize를 사용.
    explicit Town(int64 stageId);
    ~Town() override = default;

    Town(const Town&) = delete;
    Town& operator=(const Town&) = delete;

protected:
    void OnStageUpdate(int64 deltaMs) override;

private:
    // 위임 생성자용 private 생성자. StageGridParams를 1회만 평가하기 위해 사용.
    Town(int64 stageId, const StageGridParams& params);
};

using TownPtr = std::shared_ptr<Town>;
