// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. GameEnum.xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include "GameEnum_Buff.h"

EBuffCategory StringToBuffCategory(const std::string& v)
{
    if (v == "None") return EBuffCategory::None;
    if (v == "Buff") return EBuffCategory::Buff;
    if (v == "Debuff") return EBuffCategory::Debuff;
    return EBuffCategory::None;
}

std::string BuffCategoryToString(EBuffCategory v)
{
    switch (v)
    {
    case EBuffCategory::None: return "None";
    case EBuffCategory::Buff: return "Buff";
    case EBuffCategory::Debuff: return "Debuff";
    default: return "None";
    }
}

EBuffStackPolicy StringToBuffStackPolicy(const std::string& v)
{
    if (v == "None") return EBuffStackPolicy::None;
    if (v == "Refresh") return EBuffStackPolicy::Refresh;
    if (v == "Stack") return EBuffStackPolicy::Stack;
    if (v == "Ignore") return EBuffStackPolicy::Ignore;
    return EBuffStackPolicy::None;
}

std::string BuffStackPolicyToString(EBuffStackPolicy v)
{
    switch (v)
    {
    case EBuffStackPolicy::None: return "None";
    case EBuffStackPolicy::Refresh: return "Refresh";
    case EBuffStackPolicy::Stack: return "Stack";
    case EBuffStackPolicy::Ignore: return "Ignore";
    default: return "None";
    }
}

EPeriodicEffect StringToPeriodicEffect(const std::string& v)
{
    if (v == "None") return EPeriodicEffect::None;
    if (v == "DamageHp") return EPeriodicEffect::DamageHp;
    if (v == "HealHp") return EPeriodicEffect::HealHp;
    return EPeriodicEffect::None;
}

std::string PeriodicEffectToString(EPeriodicEffect v)
{
    switch (v)
    {
    case EPeriodicEffect::None: return "None";
    case EPeriodicEffect::DamageHp: return "DamageHp";
    case EPeriodicEffect::HealHp: return "HealHp";
    default: return "None";
    }
}

