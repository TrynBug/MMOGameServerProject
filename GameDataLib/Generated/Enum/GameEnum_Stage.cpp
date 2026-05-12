// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. GameEnum.xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include "GameEnum_Stage.h"

EStageType StringToStageType(const std::string& v)
{
    if (v == "None") return EStageType::None;
    if (v == "Town") return EStageType::Town;
    if (v == "Field") return EStageType::Field;
    if (v == "Dungeon") return EStageType::Dungeon;
    return EStageType::None;
}

std::string StageTypeToString(EStageType v)
{
    switch (v)
    {
    case EStageType::None: return "None";
    case EStageType::Town: return "Town";
    case EStageType::Field: return "Field";
    case EStageType::Dungeon: return "Dungeon";
    default: return "None";
    }
}

