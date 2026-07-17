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
#include "Enum/GameEnum_Skill.h"

struct GameData_Skill;


// Skill 데이터 1개 행을 표현합니다.
struct GameDataBase_Skill : public GameData
{
    int32_t              Key                  = 0;
    std::string          Name                 = "";
    EJob                 Job                  = EJob::None;
    ESkillCastClass      CastClass            = ESkillCastClass::None;
    bool                 IsPrimaryEligible    = false;
    int32_t              OnHitSkillKey        = 0;
    int32_t              NextSkillKey         = 0;
    ENextSkillTiming     NextTriggerTiming    = ENextSkillTiming::None;
    int32_t              NextTriggerDelayMs   = 0;
    ENextSkillOrigin     NextOrigin           = ENextSkillOrigin::None;
    float                CasterFrontDistance  = 0.0f;
    int32_t              CooldownMs           = 0;
    float                ManaCost             = 0.0f;
    int32_t              CastDelayMs          = 0;
    int32_t              ActionLockMs         = 0;
    bool                 Rotation             = true;
    ETargetingMode       Targeting            = ETargetingMode::None;
    ESkillPlacement      Placement            = ESkillPlacement::None;
    float                EffectCenterForwardOffset = 0.0f;
    ESkillEffectMotion   EffectMotion         = ESkillEffectMotion::None;
    ESkillEffectDamage   EffectDamage         = ESkillEffectDamage::None;
    ESkillEffectShape    EffectShape          = ESkillEffectShape::None;
    double               DamageCoeff          = 0;
    float                Radius               = 0.0f;
    float                ObbWidth             = 0.0f;
    float                ObbLength            = 0.0f;
    float                ProjectileSpeed      = 0.0f;
    float                MaxRange             = 0.0f;
    int32_t              ProjectileCount      = 0;
    float                FanAngleDeg          = 0.0f;
    int32_t              FirstTickDelayMs     = 0;
    int32_t              TickIntervalMs       = 0;
    int32_t              LifetimeMs           = 0;
    float                MoveDistance         = 0.0f;
    int32_t              MoveDurationMs       = 0;
    int32_t              ScatterCount         = 0;
    float                ScatterInnerRadius   = 0.0f;
    float                ScatterOuterRadius   = 0.0f;
};


// Skill 데이터 파일 전체를 표현합니다.
class GameDataTableBase_Skill : public GameDataTable
{

public:
    static constexpr const std::string_view k_dataName = "Skill";

protected:
    GameDataTableBase_Skill() = default;
    virtual ~GameDataTableBase_Skill() = default;

public:
    static const GameData_Skill* FindData(int32_t key);
    static const std::map<int32_t, const GameData_Skill*>& GetDataMap() { return sm_dataMap; }

public:
    const char* GetDataName() override { return "Skill"; }

protected:
    virtual bool makeGameData(const std::string& line) override;

protected:
    inline static std::map<int32_t, const GameData_Skill*> sm_dataMap;
};
