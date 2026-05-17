// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. GameEnum.xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include "GameEnum_Monster.h"

EMonsterGrade StringToMonsterGrade(const std::string& v)
{
    if (v == "None") return EMonsterGrade::None;
    if (v == "Normal") return EMonsterGrade::Normal;
    if (v == "Magic") return EMonsterGrade::Magic;
    if (v == "Rare") return EMonsterGrade::Rare;
    if (v == "Boss") return EMonsterGrade::Boss;
    return EMonsterGrade::None;
}

std::string MonsterGradeToString(EMonsterGrade v)
{
    switch (v)
    {
    case EMonsterGrade::None: return "None";
    case EMonsterGrade::Normal: return "Normal";
    case EMonsterGrade::Magic: return "Magic";
    case EMonsterGrade::Rare: return "Rare";
    case EMonsterGrade::Boss: return "Boss";
    default: return "None";
    }
}

