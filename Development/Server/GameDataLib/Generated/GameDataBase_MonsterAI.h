#pragma once
// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include <map>
#include <string>

#include "LoggerLib.h"
#include "../GameData.h"

#include "Enum/GameEnum_Monster.h"

struct GameData_MonsterAI;


// MonsterAI 데이터 1개 행을 표현합니다.
struct GameDataBase_MonsterAI : public GameData
{
    int32_t              Key                  = 0;
    std::string          Name                 = "";
    EMonsterAIType       AIType               = EMonsterAIType::None;
    float                AggroRange           = 0.0f;
    float                LeashRange           = 0.0f;
    float                DesiredRange         = 0.0f;
    int32_t              EngagedUpdateIntervalMs = 100;
    float                WanderRadius         = 0.0f;
    int32_t              WanderMinIntervalMs  = 3000;
    int32_t              WanderMaxIntervalMs  = 8000;
};


// MonsterAI 데이터 파일 전체를 표현합니다.
class GameDataTableBase_MonsterAI : public GameDataTable
{

public:
    static constexpr const std::string_view k_dataName = "MonsterAI";

protected:
    GameDataTableBase_MonsterAI() = default;
    virtual ~GameDataTableBase_MonsterAI() = default;

public:
    static const GameData_MonsterAI* FindData(int32_t key);
    static const std::map<int32_t, const GameData_MonsterAI*>& GetDataMap() { return sm_dataMap; }

public:
    const char* GetDataName() override { return "MonsterAI"; }

protected:
    virtual bool makeGameData(const std::string& line) override;

protected:
    inline static std::map<int32_t, const GameData_MonsterAI*> sm_dataMap;
};
