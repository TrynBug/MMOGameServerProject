#pragma once
// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include <map>
#include <string>

#include "LoggerLib.h"
#include "../GameData.h"

#include "Enum/GameEnum_Prop.h"
#include "Enum/GameEnum_Stage.h"

struct GameData_Prop;


// Prop 데이터 1개 행을 표현합니다.
struct GameDataBase_Prop : public GameData
{
    int32_t              Key                  = 0;
    bool                 Interactable         = true;
    EPropStateMode       StateMode            = EPropStateMode::None;
    int32_t              StateCount           = 2;
    int32_t              InitialState         = 0;
    int32_t              MaxInteract          = 0;
    int32_t              CooldownMs           = 0;
    float                InteractRange        = 2.0f;
    int32_t              DespawnDelayMs       = 0;
    EPropBehavior        Behavior             = EPropBehavior::None;
    int32_t              PortalStageKey       = 0;
    EStagePositionType   PortalPositionType   = EStagePositionType::Default;
};


// Prop 데이터 파일 전체를 표현합니다.
class GameDataTableBase_Prop : public GameDataTable
{

public:
    static constexpr const std::string_view k_dataName = "Prop";

protected:
    GameDataTableBase_Prop() = default;
    virtual ~GameDataTableBase_Prop() = default;

public:
    static const GameData_Prop* FindData(int32_t key);
    static const std::map<int32_t, const GameData_Prop*>& GetDataMap() { return sm_dataMap; }

public:
    const char* GetDataName() override { return "Prop"; }

protected:
    virtual bool makeGameData(const std::string& line) override;

protected:
    inline static std::map<int32_t, const GameData_Prop*> sm_dataMap;
};
