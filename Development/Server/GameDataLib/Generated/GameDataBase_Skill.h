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
    bool                 IsUserCastable       = false;
    int64_t              NextSkillKey         = 0;
    double               TriggerDelay         = 0;
    EJob                 Job                  = EJob::None;
    ESkillCategory       Category             = ESkillCategory::None;
    double               CoolTime             = 0;
    double               ManaCost             = 0;
    double               CastTime             = 0;
    bool                 NeedsTargeting       = true;
    EEffectType          EffectType           = EEffectType::None;
    double               Damage               = 0;
    EOriginType          OriginType           = EOriginType::None;
    ERangeShape          RangeShape           = ERangeShape::None;
    double               RangeX               = 0;
    double               RangeY               = 0;
    double               IntervalSec          = 0;
    double               FirstHitDelay        = 0;
    double               DurationSec          = 0;
    int64_t              ProjectileCount      = 0;
    double               ProjectileSpreadAngle = 0;
    double               ProjectileSpeed      = 0;
    double               ProjectileRange      = 0;
    int64_t              ProjectileMaxHitPerTarget = 0;
    double               MoveDistance         = 0;
    double               MoveTime             = 0;
    double               MoveFastPhaseRatio   = 0;
    double               MoveFastTimeRatio    = 0;
    int64_t              MaxTargetCount       = 0;
    double               KnockbackDistance    = 0;
    int64_t              BuffKey              = 0;
    int64_t              SlowOnHitKey         = 0;
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
