#pragma once

#include "pch.h"
#include "Stages/Stage.h"

// 마을 Stage
// 캐릭터 선택이 완료된 유저가 입장하는 기본 Stage.
// 게임서버당 1개 존재.
class Town : public Stage
{
public:
    // GameData_Stage 키로 생성. 해당 데이터의 stageType/sectorSize 는 사용하고,
    // worldMin/Max 는 LoadStageGridParams 의 fallback 값이 사용된다 (NavMesh 메타가 없을 때용).
    explicit Town(int64 stageId, int32 stageDataKey);

    // 명시적 params 로 생성. StageManager 에서 NavMesh 메타로 worldMin/Max 를 덮어쓴 params 를 보낸다.
    Town(int64 stageId, int32 stageDataKey, const StageGridParams& params);

    ~Town() override = default;

    Town(const Town&) = delete;
    Town& operator=(const Town&) = delete;

protected:
    void OnStageUpdate(int64 deltaMs) override;

    // 마을 시작 시 1회 호출.
    void OnStart() override;
};

using TownPtr = std::shared_ptr<Town>;
