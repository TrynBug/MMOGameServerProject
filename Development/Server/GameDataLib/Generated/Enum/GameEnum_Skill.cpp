// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. GameEnum.xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include "GameEnum_Skill.h"

ESkillCategory StringToSkillCategory(const std::string& v)
{
    if (v == "None") return ESkillCategory::None;
    if (v == "StationaryCast") return ESkillCategory::StationaryCast;
    if (v == "MobileCast") return ESkillCategory::MobileCast;
    if (v == "Movement") return ESkillCategory::Movement;
    return ESkillCategory::None;
}

std::string SkillCategoryToString(ESkillCategory v)
{
    switch (v)
    {
    case ESkillCategory::None: return "None";
    case ESkillCategory::StationaryCast: return "StationaryCast";
    case ESkillCategory::MobileCast: return "MobileCast";
    case ESkillCategory::Movement: return "Movement";
    default: return "None";
    }
}

EEffectType StringToEffectType(const std::string& v)
{
    if (v == "None") return EEffectType::None;
    if (v == "Movement") return EEffectType::Movement;
    if (v == "Projectile") return EEffectType::Projectile;
    if (v == "InstantDamage") return EEffectType::InstantDamage;
    if (v == "TickDamageArea") return EEffectType::TickDamageArea;
    if (v == "Buff") return EEffectType::Buff;
    if (v == "VFX") return EEffectType::VFX;
    return EEffectType::None;
}

std::string EffectTypeToString(EEffectType v)
{
    switch (v)
    {
    case EEffectType::None: return "None";
    case EEffectType::Movement: return "Movement";
    case EEffectType::Projectile: return "Projectile";
    case EEffectType::InstantDamage: return "InstantDamage";
    case EEffectType::TickDamageArea: return "TickDamageArea";
    case EEffectType::Buff: return "Buff";
    case EEffectType::VFX: return "VFX";
    default: return "None";
    }
}

ERangeShape StringToRangeShape(const std::string& v)
{
    if (v == "None") return ERangeShape::None;
    if (v == "Circle") return ERangeShape::Circle;
    if (v == "Rectangle") return ERangeShape::Rectangle;
    if (v == "Sector") return ERangeShape::Sector;
    return ERangeShape::None;
}

std::string RangeShapeToString(ERangeShape v)
{
    switch (v)
    {
    case ERangeShape::None: return "None";
    case ERangeShape::Circle: return "Circle";
    case ERangeShape::Rectangle: return "Rectangle";
    case ERangeShape::Sector: return "Sector";
    default: return "None";
    }
}

EOriginType StringToOriginType(const std::string& v)
{
    if (v == "None") return EOriginType::None;
    if (v == "CasterCenter") return EOriginType::CasterCenter;
    if (v == "TargetCenter") return EOriginType::TargetCenter;
    if (v == "CasterForward") return EOriginType::CasterForward;
    return EOriginType::None;
}

std::string OriginTypeToString(EOriginType v)
{
    switch (v)
    {
    case EOriginType::None: return "None";
    case EOriginType::CasterCenter: return "CasterCenter";
    case EOriginType::TargetCenter: return "TargetCenter";
    case EOriginType::CasterForward: return "CasterForward";
    default: return "None";
    }
}

