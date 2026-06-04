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
    int64_t              Key                  = 0;
    std::string          Name                 = "";
    EJob                 Job                  = EJob::None;
    ESkillCastClass      CastClass            = ESkillCastClass::None;
    bool                 IsPrimaryEligible    = false;
    int64_t              OnHitSkillKey        = 0;
    int64_t              NextSkillKey         = 0;
    ENextSkillTiming     NextTriggerTiming    = ENextSkillTiming::None;
    int64_t              NextTriggerDelayMs   = 0;
    ENextSkillOrigin     NextOrigin           = ENextSkillOrigin::None;
    double               CasterFrontDistance  = 0;
    int64_t              CooldownMs           = 0;
    double               ManaCost             = 0;
    int64_t              CastDelayMs          = 0;
    int64_t              ActionLockMs         = 0;
    bool                 Rotation             = true;
    ESkillEffectMotion   EffectMotion         = ESkillEffectMotion::None;
    ESkillEffectDamage   EffectDamage         = ESkillEffectDamage::None;
    ESkillEffectShape    EffectShape          = ESkillEffectShape::None;
    double               DamageCoeff          = 0;
    double               Radius               = 0;
    double               ObbWidth             = 0;
    double               ObbLength            = 0;
    double               ProjectileSpeed      = 0;
    double               MaxRange             = 0;
    int64_t              ProjectileCount      = 0;
    double               FanAngleDeg          = 0;
    int64_t              FirstTickDelayMs     = 0;
    int64_t              TickIntervalMs       = 0;
    int64_t              LifetimeMs           = 0;
    double               MoveDistance         = 0;
    int64_t              MoveDurationMs       = 0;
    int64_t              ScatterCount         = 0;
    double               ScatterInnerRadius   = 0;
    double               ScatterOuterRadius   = 0;
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
    static const GameData_Skill* FindData(int64_t key);
    static const std::map<int64_t, const GameData_Skill*>& GetDataMap() { return sm_dataMap; }

public:
    const char* GetDataName() override { return "Skill"; }

protected:
    virtual bool makeGameData(const std::string& line) override;

protected:
    inline static std::map<int64_t, const GameData_Skill*> sm_dataMap;
};
