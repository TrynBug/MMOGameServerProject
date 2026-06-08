#pragma once
// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include <map>
#include <string>

#include "LoggerLib.h"
#include "../GameData.h"

#include "Enum/GameEnum_Stat.h"

struct GameData_Stat;


// Stat 데이터 1개 행을 표현합니다.
struct GameDataBase_Stat : public GameData
{
    int32_t              Key                  = 0;
    EStat                Stat                 = EStat::None;
    EStatGroup           StatGroup            = EStatGroup::None;
    EStatOp              StatOp               = EStatOp::None;
    double               MinApplyValue        = -1000000000;
    double               MaxApplyValue        = 1000000000;
};


// Stat 데이터 파일 전체를 표현합니다.
class GameDataTableBase_Stat : public GameDataTable
{

public:
    static constexpr const std::string_view k_dataName = "Stat";

protected:
    GameDataTableBase_Stat() = default;
    virtual ~GameDataTableBase_Stat() = default;

public:
    static const GameData_Stat* FindData(int32_t key);
    static const std::map<int32_t, const GameData_Stat*>& GetDataMap() { return sm_dataMap; }

public:
    const char* GetDataName() override { return "Stat"; }

protected:
    virtual bool makeGameData(const std::string& line) override;

protected:
    inline static std::map<int32_t, const GameData_Stat*> sm_dataMap;
};
