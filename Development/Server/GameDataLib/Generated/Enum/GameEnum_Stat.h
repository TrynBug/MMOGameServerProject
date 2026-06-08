#pragma once
// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. GameEnum.xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include <string>

enum class EStatOp : int
{
    None                 = 0,
    Add                  = 1,  // 합연산 (힘+)
    AddPct               = 2,  // 합연산% (힘+%)
    Amp                  = 3,  // 곱연산 (힘증폭)
    Reduce               = 4,  // 점감곱연산 (힘감폭)
    Total                = 5,  // 총합
    Max                 
};

EStatOp StringToStatOp(const std::string& v);
std::string StatOpToString(EStatOp v);

enum class EStatGroup : int
{
    None                 = 0,
    Str                  = 1,  // 힘
    Int                  = 2,  // 지능
    Hp                   = 3,  // HP
    Mp                   = 4,  // MP
    PDef                 = 5,  // 물리방어력
    MDef                 = 6,  // 마법방어력
    MoveSpd              = 7,  // 이동속도
    AtkSpd               = 8,  // 공격속도
    CritRate             = 9,  // 크리티컬확률
    Max                 
};

EStatGroup StringToStatGroup(const std::string& v);
std::string StatGroupToString(EStatGroup v);

enum class EStat : int
{
    None                 = 0,
    StrAdd               = 1,  // 힘+
    StrAddPct            = 2,  // 힘+%
    StrAmp               = 3,  // 힘증폭
    StrReduce            = 4,  // 힘감폭
    StrTotal             = 5,  // 힘총합
    IntAdd               = 6,  // 지능+
    IntAddPct            = 7,  // 지능+%
    IntAmp               = 8,  // 지능증폭
    IntReduce            = 9,  // 지능감폭
    IntTotal             = 10,  // 지능총합
    HpAdd                = 11,  // HP+
    HpAddPct             = 12,  // HP+%
    HpAmp                = 13,  // HP증폭
    HpReduce             = 14,  // HP감폭
    HpTotal              = 15,  // HP총합
    MpAdd                = 16,  // MP+
    MpAddPct             = 17,  // MP+%
    MpAmp                = 18,  // MP증폭
    MpReduce             = 19,  // MP감폭
    MpTotal              = 20,  // MP총합
    PDefAdd              = 21,  // 물리방어력+
    PDefAddPct           = 22,  // 물리방어력+%
    PDefAmp              = 23,  // 물리방어력증폭
    PDefReduce           = 24,  // 물리방어력감폭
    PDefTotal            = 25,  // 물리방어력총합
    MDefAdd              = 26,  // 마법방어력+
    MDefAddPct           = 27,  // 마법방어력+%
    MDefAmp              = 28,  // 마법방어력증폭
    MDefReduce           = 29,  // 마법방어력감폭
    MDefTotal            = 30,  // 마법방어력총합
    MoveSpdAdd           = 31,  // 이동속도+
    MoveSpdAddPct        = 32,  // 이동속도+%
    MoveSpdAmp           = 33,  // 이동속도증폭
    MoveSpdReduce        = 34,  // 이동속도감폭
    MoveSpdTotal         = 35,  // 이동속도총합
    AtkSpdAdd            = 36,  // 공격속도+
    AtkSpdAddPct         = 37,  // 공격속도+%
    AtkSpdAmp            = 38,  // 공격속도증폭
    AtkSpdReduce         = 39,  // 공격속도감폭
    AtkSpdTotal          = 40,  // 공격속도총합
    CritRateAdd          = 41,  // 치명타확률+
    CritRateAddPct       = 42,  // 치명타확률+%
    CritRateAmp          = 43,  // 치명타확률증폭
    CritRateReduce       = 44,  // 치명타확률감폭
    CritRateTotal        = 45,  // 치명타확률총합
    Max                 
};

EStat StringToStat(const std::string& v);
std::string StatToString(EStat v);

