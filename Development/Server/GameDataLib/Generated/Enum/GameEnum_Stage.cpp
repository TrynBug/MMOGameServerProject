// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. GameEnum.xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include "GameEnum_Stage.h"

EStageType StringToStageType(const std::string& v)
{
    if (v == "None") return EStageType::None;
    if (v == "System") return EStageType::System;
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
    case EStageType::System: return "System";
    case EStageType::Town: return "Town";
    case EStageType::Field: return "Field";
    case EStageType::Dungeon: return "Dungeon";
    default: return "None";
    }
}

EStagePositionType StringToStagePositionType(const std::string& v)
{
    if (v == "None") return EStagePositionType::None;
    if (v == "Default") return EStagePositionType::Default;
    if (v == "Path1") return EStagePositionType::Path1;
    if (v == "Path2") return EStagePositionType::Path2;
    if (v == "Path3") return EStagePositionType::Path3;
    return EStagePositionType::None;
}

std::string StagePositionTypeToString(EStagePositionType v)
{
    switch (v)
    {
    case EStagePositionType::None: return "None";
    case EStagePositionType::Default: return "Default";
    case EStagePositionType::Path1: return "Path1";
    case EStagePositionType::Path2: return "Path2";
    case EStagePositionType::Path3: return "Path3";
    default: return "None";
    }
}

