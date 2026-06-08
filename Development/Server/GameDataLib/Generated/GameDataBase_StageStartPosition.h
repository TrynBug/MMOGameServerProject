#pragma once
// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include <map>
#include <string>

#include "LoggerLib.h"
#include "../GameData.h"

#include "Enum/GameEnum_Stage.h"

struct GameData_StageStartPosition;


// StageStartPosition 데이터 1개 행을 표현합니다.
struct GameDataBase_StageStartPosition : public GameData
{
    int32_t              Key                  = 0;
    int32_t              StageKey             = 0;
    EStagePositionType   StagePositionType    = EStagePositionType::None;
    float                PosX                 = 0.0f;
    float                PosY                 = 0.0f;
    float                PosZ                 = 0.0f;
    float                Yaw                  = 0.0f;
};


// StageStartPosition 데이터 파일 전체를 표현합니다.
class GameDataTableBase_StageStartPosition : public GameDataTable
{

public:
    static constexpr const std::string_view k_dataName = "StageStartPosition";

protected:
    GameDataTableBase_StageStartPosition() = default;
    virtual ~GameDataTableBase_StageStartPosition() = default;

public:
    static const GameData_StageStartPosition* FindData(int64_t key);
    static const std::map<int64_t, const GameData_StageStartPosition*>& GetDataMap() { return sm_dataMap; }

public:
    const char* GetDataName() override { return "StageStartPosition"; }

protected:
    virtual bool makeGameData(const std::string& line) override;

protected:
    inline static std::map<int64_t, const GameData_StageStartPosition*> sm_dataMap;
};
