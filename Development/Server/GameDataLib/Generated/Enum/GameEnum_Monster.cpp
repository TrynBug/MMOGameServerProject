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
    if (v == "Unique") return EMonsterGrade::Unique;
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
    case EMonsterGrade::Unique: return "Unique";
    case EMonsterGrade::Boss: return "Boss";
    default: return "None";
    }
}

EMonsterAIType StringToMonsterAIType(const std::string& v)
{
    if (v == "None") return EMonsterAIType::None;
    if (v == "FSM") return EMonsterAIType::FSM;
    if (v == "BehaviourTree") return EMonsterAIType::BehaviourTree;
    return EMonsterAIType::None;
}

std::string MonsterAITypeToString(EMonsterAIType v)
{
    switch (v)
    {
    case EMonsterAIType::None: return "None";
    case EMonsterAIType::FSM: return "FSM";
    case EMonsterAIType::BehaviourTree: return "BehaviourTree";
    default: return "None";
    }
}

ESpawnActivation StringToSpawnActivation(const std::string& v)
{
    if (v == "None") return ESpawnActivation::None;
    if (v == "Always") return ESpawnActivation::Always;
    if (v == "PlayerProximity") return ESpawnActivation::PlayerProximity;
    if (v == "Manual") return ESpawnActivation::Manual;
    return ESpawnActivation::None;
}

std::string SpawnActivationToString(ESpawnActivation v)
{
    switch (v)
    {
    case ESpawnActivation::None: return "None";
    case ESpawnActivation::Always: return "Always";
    case ESpawnActivation::PlayerProximity: return "PlayerProximity";
    case ESpawnActivation::Manual: return "Manual";
    default: return "None";
    }
}

