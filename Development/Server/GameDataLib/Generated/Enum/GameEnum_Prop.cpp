// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. GameEnum.xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include "GameEnum_Prop.h"

EPropStateMode StringToPropStateMode(const std::string& v)
{
    if (v == "None") return EPropStateMode::None;
    if (v == "Toggle") return EPropStateMode::Toggle;
    if (v == "OneShot") return EPropStateMode::OneShot;
    return EPropStateMode::None;
}

std::string PropStateModeToString(EPropStateMode v)
{
    switch (v)
    {
    case EPropStateMode::None: return "None";
    case EPropStateMode::Toggle: return "Toggle";
    case EPropStateMode::OneShot: return "OneShot";
    default: return "None";
    }
}

EPropBehavior StringToPropBehavior(const std::string& v)
{
    if (v == "None") return EPropBehavior::None;
    if (v == "Portal") return EPropBehavior::Portal;
    return EPropBehavior::None;
}

std::string PropBehaviorToString(EPropBehavior v)
{
    switch (v)
    {
    case EPropBehavior::None: return "None";
    case EPropBehavior::Portal: return "Portal";
    default: return "None";
    }
}

