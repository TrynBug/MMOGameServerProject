#pragma once
// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include <map>
#include <string>

#include "LoggerLib.h"
#include "../GameData.h"

#include "Enum/GameEnum_Buff.h"
#include "Enum/GameEnum_Stat.h"

struct GameData_Buff;


// Buff 데이터 1개 행을 표현합니다.
struct GameDataBase_Buff : public GameData
{
    int64_t              Key                  = 0;
    std::string          Name                 = "";
    std::string          Desc                 = "";
    EBuffCategory        Category             = EBuffCategory::None;
    int64_t              DurationMs           = 0;
    int64_t              MaxStack             = 1;
    EBuffStackPolicy     StackPolicy          = EBuffStackPolicy::Refresh;
    bool                 Dispellable          = false;
    bool                 RemoveOnDeath        = true;
    bool                 RemoveOnStageChange  = true;
    int64_t              TickIntervalMs       = 0;
    EPeriodicEffect      PeriodicType         = EPeriodicEffect::None;
    double               PeriodicValue        = 0;
    EStat                Stat1                = EStat::None;
    double               StatValue1           = 0;
    EStat                Stat2                = EStat::None;
    double               StatValue2           = 0;
    EStat                Stat3                = EStat::None;
    double               StatValue3           = 0;

    int32_t GetStatCount() const { return 3; }
    EStat GetStat(int32_t index) const
    {
        switch (index)
        {
        case 0: return Stat1;
        case 1: return Stat2;
        case 2: return Stat3;
        default: return EStat::None;
        }
    }

    int32_t GetStatValueCount() const { return 3; }
    double GetStatValue(int32_t index) const
    {
        switch (index)
        {
        case 0: return StatValue1;
        case 1: return StatValue2;
        case 2: return StatValue3;
        default: return 0;
        }
    }
};


// Buff 데이터 파일 전체를 표현합니다.
class GameDataTableBase_Buff : public GameDataTable
{

public:
    static constexpr const std::string_view k_dataName = "Buff";

protected:
    GameDataTableBase_Buff() = default;
    virtual ~GameDataTableBase_Buff() = default;

public:
    static const GameData_Buff* FindData(int64_t key);
    static const std::map<int64_t, const GameData_Buff*>& GetDataMap() { return sm_dataMap; }

public:
    const char* GetDataName() override { return "Buff"; }

protected:
    virtual bool makeGameData(const std::string& line) override;

protected:
    inline static std::map<int64_t, const GameData_Buff*> sm_dataMap;
};
