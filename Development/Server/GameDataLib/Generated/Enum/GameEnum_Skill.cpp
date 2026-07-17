// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. GameEnum.xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include "GameEnum_Skill.h"

ESkillCastClass StringToSkillCastClass(const std::string& v)
{
    if (v == "None") return ESkillCastClass::None;
    if (v == "Stationary") return ESkillCastClass::Stationary;
    if (v == "Mobile") return ESkillCastClass::Mobile;
    if (v == "Mobility") return ESkillCastClass::Mobility;
    return ESkillCastClass::None;
}

std::string SkillCastClassToString(ESkillCastClass v)
{
    switch (v)
    {
    case ESkillCastClass::None: return "None";
    case ESkillCastClass::Stationary: return "Stationary";
    case ESkillCastClass::Mobile: return "Mobile";
    case ESkillCastClass::Mobility: return "Mobility";
    default: return "None";
    }
}

ENextSkillOrigin StringToNextSkillOrigin(const std::string& v)
{
    if (v == "None") return ENextSkillOrigin::None;
    if (v == "CasterPos") return ENextSkillOrigin::CasterPos;
    if (v == "CasterFront") return ENextSkillOrigin::CasterFront;
    if (v == "PrevCenter") return ENextSkillOrigin::PrevCenter;
    if (v == "PrevEnd") return ENextSkillOrigin::PrevEnd;
    return ENextSkillOrigin::None;
}

std::string NextSkillOriginToString(ENextSkillOrigin v)
{
    switch (v)
    {
    case ENextSkillOrigin::None: return "None";
    case ENextSkillOrigin::CasterPos: return "CasterPos";
    case ENextSkillOrigin::CasterFront: return "CasterFront";
    case ENextSkillOrigin::PrevCenter: return "PrevCenter";
    case ENextSkillOrigin::PrevEnd: return "PrevEnd";
    default: return "None";
    }
}

ENextSkillTiming StringToNextSkillTiming(const std::string& v)
{
    if (v == "None") return ENextSkillTiming::None;
    if (v == "OnStart") return ENextSkillTiming::OnStart;
    if (v == "AfterEnd") return ENextSkillTiming::AfterEnd;
    if (v == "AfterDelay") return ENextSkillTiming::AfterDelay;
    return ENextSkillTiming::None;
}

std::string NextSkillTimingToString(ENextSkillTiming v)
{
    switch (v)
    {
    case ENextSkillTiming::None: return "None";
    case ENextSkillTiming::OnStart: return "OnStart";
    case ENextSkillTiming::AfterEnd: return "AfterEnd";
    case ENextSkillTiming::AfterDelay: return "AfterDelay";
    default: return "None";
    }
}

ESkillEffectMotion StringToSkillEffectMotion(const std::string& v)
{
    if (v == "None") return ESkillEffectMotion::None;
    if (v == "Static") return ESkillEffectMotion::Static;
    if (v == "Linear") return ESkillEffectMotion::Linear;
    return ESkillEffectMotion::None;
}

std::string SkillEffectMotionToString(ESkillEffectMotion v)
{
    switch (v)
    {
    case ESkillEffectMotion::None: return "None";
    case ESkillEffectMotion::Static: return "Static";
    case ESkillEffectMotion::Linear: return "Linear";
    default: return "None";
    }
}

ESkillEffectDamage StringToSkillEffectDamage(const std::string& v)
{
    if (v == "None") return ESkillEffectDamage::None;
    if (v == "ContactHit") return ESkillEffectDamage::ContactHit;
    if (v == "Area") return ESkillEffectDamage::Area;
    return ESkillEffectDamage::None;
}

std::string SkillEffectDamageToString(ESkillEffectDamage v)
{
    switch (v)
    {
    case ESkillEffectDamage::None: return "None";
    case ESkillEffectDamage::ContactHit: return "ContactHit";
    case ESkillEffectDamage::Area: return "Area";
    default: return "None";
    }
}

ESkillEffectShape StringToSkillEffectShape(const std::string& v)
{
    if (v == "None") return ESkillEffectShape::None;
    if (v == "Circle") return ESkillEffectShape::Circle;
    if (v == "Obb") return ESkillEffectShape::Obb;
    return ESkillEffectShape::None;
}

std::string SkillEffectShapeToString(ESkillEffectShape v)
{
    switch (v)
    {
    case ESkillEffectShape::None: return "None";
    case ESkillEffectShape::Circle: return "Circle";
    case ESkillEffectShape::Obb: return "Obb";
    default: return "None";
    }
}

ETargetingMode StringToTargetingMode(const std::string& v)
{
    if (v == "None") return ETargetingMode::None;
    if (v == "Nearest") return ETargetingMode::Nearest;
    if (v == "HighestGradeNearest") return ETargetingMode::HighestGradeNearest;
    return ETargetingMode::None;
}

std::string TargetingModeToString(ETargetingMode v)
{
    switch (v)
    {
    case ETargetingMode::None: return "None";
    case ETargetingMode::Nearest: return "Nearest";
    case ETargetingMode::HighestGradeNearest: return "HighestGradeNearest";
    default: return "None";
    }
}

ESkillPlacement StringToSkillPlacement(const std::string& v)
{
    if (v == "None") return ESkillPlacement::None;
    if (v == "Caster") return ESkillPlacement::Caster;
    if (v == "SkillCastOrigin") return ESkillPlacement::SkillCastOrigin;
    if (v == "Target") return ESkillPlacement::Target;
    return ESkillPlacement::None;
}

std::string SkillPlacementToString(ESkillPlacement v)
{
    switch (v)
    {
    case ESkillPlacement::None: return "None";
    case ESkillPlacement::Caster: return "Caster";
    case ESkillPlacement::SkillCastOrigin: return "SkillCastOrigin";
    case ESkillPlacement::Target: return "Target";
    default: return "None";
    }
}

