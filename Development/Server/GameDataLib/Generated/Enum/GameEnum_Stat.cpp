// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. GameEnum.xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include "GameEnum_Stat.h"

EStat StringToStat(const std::string& v)
{
    if (v == "None") return EStat::None;
    if (v == "AddHP") return EStat::AddHP;
    if (v == "AddMP") return EStat::AddMP;
    if (v == "AddAtk") return EStat::AddAtk;
    return EStat::None;
}

std::string StatToString(EStat v)
{
    switch (v)
    {
    case EStat::None: return "None";
    case EStat::AddHP: return "AddHP";
    case EStat::AddMP: return "AddMP";
    case EStat::AddAtk: return "AddAtk";
    default: return "None";
    }
}

