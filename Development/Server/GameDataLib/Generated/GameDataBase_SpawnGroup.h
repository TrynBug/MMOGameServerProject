#pragma once
// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include <map>
#include <string>

#include "LoggerLib.h"
#include "../GameData.h"


struct GameData_SpawnGroup;


// SpawnGroup 데이터 1개 행을 표현합니다.
struct GameDataBase_SpawnGroup : public GameData
{
    int32_t              Key                  = 0;
    std::string          Name                 = "";
    int32_t              MonsterKey1          = 0;
    int32_t              MonsterCount1        = 0;
    int32_t              MonsterKey2          = 0;
    int32_t              MonsterCount2        = 0;
    int32_t              MonsterKey3          = 0;
    int32_t              MonsterCount3        = 0;
    float                ScatterRadius        = 0.0f;

    int32_t GetMonsterKeyCount() const { return 3; }
    int32_t GetMonsterKey(int32_t index) const
    {
        switch (index)
        {
        case 0: return MonsterKey1;
        case 1: return MonsterKey2;
        case 2: return MonsterKey3;
        default: return 0;
        }
    }

    int32_t GetMonsterCountCount() const { return 3; }
    int32_t GetMonsterCount(int32_t index) const
    {
        switch (index)
        {
        case 0: return MonsterCount1;
        case 1: return MonsterCount2;
        case 2: return MonsterCount3;
        default: return 0;
        }
    }
};


// SpawnGroup 데이터 파일 전체를 표현합니다.
class GameDataTableBase_SpawnGroup : public GameDataTable
{

public:
    static constexpr const std::string_view k_dataName = "SpawnGroup";

protected:
    GameDataTableBase_SpawnGroup() = default;
    virtual ~GameDataTableBase_SpawnGroup() = default;

public:
    static const GameData_SpawnGroup* FindData(int32_t key);
    static const std::map<int32_t, const GameData_SpawnGroup*>& GetDataMap() { return sm_dataMap; }

public:
    const char* GetDataName() override { return "SpawnGroup"; }

protected:
    virtual bool makeGameData(const std::string& line) override;

protected:
    inline static std::map<int32_t, const GameData_SpawnGroup*> sm_dataMap;
};
