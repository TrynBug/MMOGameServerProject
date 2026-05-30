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

EJob StringToJob(const std::string& v)
{
    if (v == "None") return EJob::None;
    if (v == "Mage") return EJob::Mage;
    if (v == "Warrior") return EJob::Warrior;
    return EJob::None;
}

std::string JobToString(EJob v)
{
    switch (v)
    {
    case EJob::None: return "None";
    case EJob::Mage: return "Mage";
    case EJob::Warrior: return "Warrior";
    default: return "None";
    }
}

EObjectType StringToObjectType(const std::string& v)
{
    if (v == "None") return EObjectType::None;
    if (v == "User") return EObjectType::User;
    if (v == "Monster") return EObjectType::Monster;
    if (v == "Prop") return EObjectType::Prop;
    if (v == "Drop") return EObjectType::Drop;
    return EObjectType::None;
}

std::string ObjectTypeToString(EObjectType v)
{
    switch (v)
    {
    case EObjectType::None: return "None";
    case EObjectType::User: return "User";
    case EObjectType::Monster: return "Monster";
    case EObjectType::Prop: return "Prop";
    case EObjectType::Drop: return "Drop";
    default: return "None";
    }
}

EResultCode StringToResultCode(const std::string& v)
{
    if (v == "None") return EResultCode::None;
    if (v == "Success") return EResultCode::Success;
    if (v == "Fail") return EResultCode::Fail;
    return EResultCode::None;
}

std::string ResultCodeToString(EResultCode v)
{
    switch (v)
    {
    case EResultCode::None: return "None";
    case EResultCode::Success: return "Success";
    case EResultCode::Fail: return "Fail";
    default: return "None";
    }
}

