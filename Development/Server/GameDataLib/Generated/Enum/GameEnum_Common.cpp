// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. GameEnum.xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include "GameEnum_Common.h"

ETeam StringToTeam(const std::string& v)
{
    if (v == "None") return ETeam::None;
    if (v == "User") return ETeam::User;
    if (v == "Monster") return ETeam::Monster;
    return ETeam::None;
}

std::string TeamToString(ETeam v)
{
    switch (v)
    {
    case ETeam::None: return "None";
    case ETeam::User: return "User";
    case ETeam::Monster: return "Monster";
    default: return "None";
    }
}

