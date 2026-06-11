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

struct GameData_Spawner;


// Spawner 데이터 1개 행을 표현합니다.
struct GameDataBase_Spawner : public GameData
{
    int32_t              Key                  = 0;
    int32_t              SpawnGroupKey        = 0;
    int32_t              MaxPacks             = 1;
    int32_t              RespawnDelayMs       = 0;
    ESpawnActivation     Activation           = ESpawnActivation::None;
    float                ActivationRange      = 0.0f;
};


// Spawner 데이터 파일 전체를 표현합니다.
class GameDataTableBase_Spawner : public GameDataTable
{

public:
    static constexpr const std::string_view k_dataName = "Spawner";

protected:
    GameDataTableBase_Spawner() = default;
    virtual ~GameDataTableBase_Spawner() = default;

public:
    static const GameData_Spawner* FindData(int32_t key);
    static const std::map<int32_t, const GameData_Spawner*>& GetDataMap() { return sm_dataMap; }

public:
    const char* GetDataName() override { return "Spawner"; }

protected:
    virtual bool makeGameData(const std::string& line) override;

protected:
    inline static std::map<int32_t, const GameData_Spawner*> sm_dataMap;
};
