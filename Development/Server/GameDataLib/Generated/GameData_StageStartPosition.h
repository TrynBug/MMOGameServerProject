#pragma once
// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 최초 1회만 생성됩니다.
// 이후에는 사용자가 직접 수정할 수 있습니다.
// =====================================================================

#include "GameDataBase_StageStartPosition.h"


// StageStartPosition 데이터 1건을 표현합니다.
struct GameData_StageStartPosition : public GameDataBase_StageStartPosition
{
public:
    bool Initialize();

    // 여기에 사용자가 추가할 멤버변수, 멤버함수를 선언합니다.
};


// StageStartPosition 데이터 전체를 관리합니다.
class GameDataTable_StageStartPosition : public GameDataTableBase_StageStartPosition
{
public:
    GameDataTable_StageStartPosition() = default;
    ~GameDataTable_StageStartPosition() = default;

public:
    virtual bool OnAddData(const GameData* pRawData) override;
    virtual bool OnLoadComplete() override;

    // (StageKey, StagePositionType) 으로 시작위치 1건 조회. 없으면 nullptr.
    // Stage 이동/입장 시 도착 좌표 결정에 사용.
    static const GameData_StageStartPosition* FindByStageAndType(int64_t stageKey, EStagePositionType positionType);

private:
    // (StageKey, StagePositionType) → 데이터. OnAddData 에서 구축.
    // 같은 조합이 중복되면 OnAddData 가 실패한다 (데이터 제작 오류).
    inline static std::map<std::pair<int64_t, EStagePositionType>, const GameData_StageStartPosition*> sm_byStageAndType;
};
