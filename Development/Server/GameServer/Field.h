#pragma once

#include "pch.h"
#include "Stage.h"

// 필드 Stage
// 마을 밖 사냥터. 게임서버 시작 시 GameData_Stage의 Field 타입 데이터마다 1개씩 생성된다.
class Field : public Stage
{
public:
    // GameData_Stage 키로 생성. 해당 데이터의 stageType/sectorSize 는 사용하고,
    // worldMin/Max 는 LoadStageGridParams 의 fallback 값이 사용된다 (NavMesh 메타가 없을 때용).
    explicit Field(int64 stageId, int32 stageDataKey);

    // 명시적 params 로 생성. StageManager 에서 NavMesh 메타로 worldMin/Max 를 덮어쓴 params 를 보낸다.
    Field(int64 stageId, int32 stageDataKey, const StageGridParams& params);

    ~Field() override = default;

    Field(const Field&) = delete;
    Field& operator=(const Field&) = delete;

protected:
    void OnStageUpdate(int64 deltaMs) override;
};

using FieldPtr = std::shared_ptr<Field>;
