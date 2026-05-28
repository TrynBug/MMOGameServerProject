// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. GameEnum.xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include "GameEnum_Stat.h"

EStatOp StringToStatOp(const std::string& v)
{
    if (v == "None") return EStatOp::None;
    if (v == "Add") return EStatOp::Add;
    if (v == "AddPct") return EStatOp::AddPct;
    if (v == "Amp") return EStatOp::Amp;
    if (v == "Reduce") return EStatOp::Reduce;
    if (v == "Total") return EStatOp::Total;
    return EStatOp::None;
}

std::string StatOpToString(EStatOp v)
{
    switch (v)
    {
    case EStatOp::None: return "None";
    case EStatOp::Add: return "Add";
    case EStatOp::AddPct: return "AddPct";
    case EStatOp::Amp: return "Amp";
    case EStatOp::Reduce: return "Reduce";
    case EStatOp::Total: return "Total";
    default: return "None";
    }
}

EStatGroup StringToStatGroup(const std::string& v)
{
    if (v == "None") return EStatGroup::None;
    if (v == "Str") return EStatGroup::Str;
    if (v == "Int") return EStatGroup::Int;
    if (v == "Hp") return EStatGroup::Hp;
    if (v == "Mp") return EStatGroup::Mp;
    if (v == "PDef") return EStatGroup::PDef;
    if (v == "MDef") return EStatGroup::MDef;
    if (v == "MoveSpd") return EStatGroup::MoveSpd;
    if (v == "AtkSpd") return EStatGroup::AtkSpd;
    if (v == "CritRate") return EStatGroup::CritRate;
    return EStatGroup::None;
}

std::string StatGroupToString(EStatGroup v)
{
    switch (v)
    {
    case EStatGroup::None: return "None";
    case EStatGroup::Str: return "Str";
    case EStatGroup::Int: return "Int";
    case EStatGroup::Hp: return "Hp";
    case EStatGroup::Mp: return "Mp";
    case EStatGroup::PDef: return "PDef";
    case EStatGroup::MDef: return "MDef";
    case EStatGroup::MoveSpd: return "MoveSpd";
    case EStatGroup::AtkSpd: return "AtkSpd";
    case EStatGroup::CritRate: return "CritRate";
    default: return "None";
    }
}

EStat StringToStat(const std::string& v)
{
    if (v == "None") return EStat::None;
    if (v == "StrAdd") return EStat::StrAdd;
    if (v == "StrAddPct") return EStat::StrAddPct;
    if (v == "StrAmp") return EStat::StrAmp;
    if (v == "StrReduce") return EStat::StrReduce;
    if (v == "StrTotal") return EStat::StrTotal;
    if (v == "IntAdd") return EStat::IntAdd;
    if (v == "IntAddPct") return EStat::IntAddPct;
    if (v == "IntAmp") return EStat::IntAmp;
    if (v == "IntReduce") return EStat::IntReduce;
    if (v == "IntTotal") return EStat::IntTotal;
    if (v == "HpAdd") return EStat::HpAdd;
    if (v == "HpAddPct") return EStat::HpAddPct;
    if (v == "HpAmp") return EStat::HpAmp;
    if (v == "HpReduce") return EStat::HpReduce;
    if (v == "HpTotal") return EStat::HpTotal;
    if (v == "MpAdd") return EStat::MpAdd;
    if (v == "MpAddPct") return EStat::MpAddPct;
    if (v == "MpAmp") return EStat::MpAmp;
    if (v == "MpReduce") return EStat::MpReduce;
    if (v == "MpTotal") return EStat::MpTotal;
    if (v == "PDefAdd") return EStat::PDefAdd;
    if (v == "PDefAddPct") return EStat::PDefAddPct;
    if (v == "PDefAmp") return EStat::PDefAmp;
    if (v == "PDefReduce") return EStat::PDefReduce;
    if (v == "PDefTotal") return EStat::PDefTotal;
    if (v == "MDefAdd") return EStat::MDefAdd;
    if (v == "MDefAddPct") return EStat::MDefAddPct;
    if (v == "MDefAmp") return EStat::MDefAmp;
    if (v == "MDefReduce") return EStat::MDefReduce;
    if (v == "MDefTotal") return EStat::MDefTotal;
    if (v == "MoveSpdAdd") return EStat::MoveSpdAdd;
    if (v == "MoveSpdAddPct") return EStat::MoveSpdAddPct;
    if (v == "MoveSpdAmp") return EStat::MoveSpdAmp;
    if (v == "MoveSpdReduce") return EStat::MoveSpdReduce;
    if (v == "MoveSpdTotal") return EStat::MoveSpdTotal;
    if (v == "AtkSpdAdd") return EStat::AtkSpdAdd;
    if (v == "AtkSpdAddPct") return EStat::AtkSpdAddPct;
    if (v == "AtkSpdAmp") return EStat::AtkSpdAmp;
    if (v == "AtkSpdReduce") return EStat::AtkSpdReduce;
    if (v == "AtkSpdTotal") return EStat::AtkSpdTotal;
    if (v == "CritRateAdd") return EStat::CritRateAdd;
    if (v == "CritRateAddPct") return EStat::CritRateAddPct;
    if (v == "CritRateAmp") return EStat::CritRateAmp;
    if (v == "CritRateReduce") return EStat::CritRateReduce;
    if (v == "CritRateTotal") return EStat::CritRateTotal;
    return EStat::None;
}

std::string StatToString(EStat v)
{
    switch (v)
    {
    case EStat::None: return "None";
    case EStat::StrAdd: return "StrAdd";
    case EStat::StrAddPct: return "StrAddPct";
    case EStat::StrAmp: return "StrAmp";
    case EStat::StrReduce: return "StrReduce";
    case EStat::StrTotal: return "StrTotal";
    case EStat::IntAdd: return "IntAdd";
    case EStat::IntAddPct: return "IntAddPct";
    case EStat::IntAmp: return "IntAmp";
    case EStat::IntReduce: return "IntReduce";
    case EStat::IntTotal: return "IntTotal";
    case EStat::HpAdd: return "HpAdd";
    case EStat::HpAddPct: return "HpAddPct";
    case EStat::HpAmp: return "HpAmp";
    case EStat::HpReduce: return "HpReduce";
    case EStat::HpTotal: return "HpTotal";
    case EStat::MpAdd: return "MpAdd";
    case EStat::MpAddPct: return "MpAddPct";
    case EStat::MpAmp: return "MpAmp";
    case EStat::MpReduce: return "MpReduce";
    case EStat::MpTotal: return "MpTotal";
    case EStat::PDefAdd: return "PDefAdd";
    case EStat::PDefAddPct: return "PDefAddPct";
    case EStat::PDefAmp: return "PDefAmp";
    case EStat::PDefReduce: return "PDefReduce";
    case EStat::PDefTotal: return "PDefTotal";
    case EStat::MDefAdd: return "MDefAdd";
    case EStat::MDefAddPct: return "MDefAddPct";
    case EStat::MDefAmp: return "MDefAmp";
    case EStat::MDefReduce: return "MDefReduce";
    case EStat::MDefTotal: return "MDefTotal";
    case EStat::MoveSpdAdd: return "MoveSpdAdd";
    case EStat::MoveSpdAddPct: return "MoveSpdAddPct";
    case EStat::MoveSpdAmp: return "MoveSpdAmp";
    case EStat::MoveSpdReduce: return "MoveSpdReduce";
    case EStat::MoveSpdTotal: return "MoveSpdTotal";
    case EStat::AtkSpdAdd: return "AtkSpdAdd";
    case EStat::AtkSpdAddPct: return "AtkSpdAddPct";
    case EStat::AtkSpdAmp: return "AtkSpdAmp";
    case EStat::AtkSpdReduce: return "AtkSpdReduce";
    case EStat::AtkSpdTotal: return "AtkSpdTotal";
    case EStat::CritRateAdd: return "CritRateAdd";
    case EStat::CritRateAddPct: return "CritRateAddPct";
    case EStat::CritRateAmp: return "CritRateAmp";
    case EStat::CritRateReduce: return "CritRateReduce";
    case EStat::CritRateTotal: return "CritRateTotal";
    default: return "None";
    }
}

