#pragma once
// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include <map>
#include <string>

#include "LoggerLib.h"
#include "../GameData.h"

#include "Enum/GameEnum_Common.h"
#include "Enum/GameEnum_Stat.h"

struct GameData_JobBase;


// JobBase 데이터 1개 행을 표현합니다.
struct GameDataBase_JobBase : public GameData
{
    int32_t              Key                  = 0;
    EJob                 Job                  = EJob::None;
    EStat                Stat1                = EStat::None;
    double               StatValue1           = 0;
    EStat                Stat2                = EStat::None;
    double               StatValue2           = 0;
    EStat                Stat3                = EStat::None;
    double               StatValue3           = 0;
    EStat                Stat4                = EStat::None;
    double               StatValue4           = 0;
    EStat                Stat5                = EStat::None;
    double               StatValue5           = 0;
    EStat                Stat6                = EStat::None;
    double               StatValue6           = 0;
    EStat                Stat7                = EStat::None;
    double               StatValue7           = 0;
    EStat                Stat8                = EStat::None;
    double               StatValue8           = 0;

    int32_t GetStatCount() const { return 8; }
    EStat GetStat(int32_t index) const
    {
        switch (index)
        {
        case 0: return Stat1;
        case 1: return Stat2;
        case 2: return Stat3;
        case 3: return Stat4;
        case 4: return Stat5;
        case 5: return Stat6;
        case 6: return Stat7;
        case 7: return Stat8;
        default: return EStat::None;
        }
    }

    int32_t GetStatValueCount() const { return 8; }
    double GetStatValue(int32_t index) const
    {
        switch (index)
        {
        case 0: return StatValue1;
        case 1: return StatValue2;
        case 2: return StatValue3;
        case 3: return StatValue4;
        case 4: return StatValue5;
        case 5: return StatValue6;
        case 6: return StatValue7;
        case 7: return StatValue8;
        default: return 0;
        }
    }
};


// JobBase 데이터 파일 전체를 표현합니다.
class GameDataTableBase_JobBase : public GameDataTable
{

public:
    static constexpr const std::string_view k_dataName = "JobBase";

protected:
    GameDataTableBase_JobBase() = default;
    virtual ~GameDataTableBase_JobBase() = default;

public:
    static const GameData_JobBase* FindData(int64_t key);
    static const std::map<int64_t, const GameData_JobBase*>& GetDataMap() { return sm_dataMap; }

public:
    const char* GetDataName() override { return "JobBase"; }

protected:
    virtual bool makeGameData(const std::string& line) override;

protected:
    inline static std::map<int64_t, const GameData_JobBase*> sm_dataMap;
};
