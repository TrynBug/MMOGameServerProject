#pragma once
// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. GameEnum.xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include <string>

enum class EBuffCategory : int
{
    None                 = 0,  // 이로운효과
    Buff                 = 1,  // 해로운효과
    Debuff               = 2,
    Max                 
};

EBuffCategory StringToBuffCategory(const std::string& v);
std::string BuffCategoryToString(EBuffCategory v);

enum class EBuffStackPolicy : int
{
    None                 = 0,
    Refresh              = 1,  // 시간갱신
    Stack                = 2,  // 스택증가
    Ignore               = 3,  // 무시
    Max                 
};

EBuffStackPolicy StringToBuffStackPolicy(const std::string& v);
std::string BuffStackPolicyToString(EBuffStackPolicy v);

enum class EPeriodicEffect : int
{
    None                 = 0,  // 주기효과 없음
    DamageHp             = 1,  // HP감소
    HealHp               = 2,  // HP회복
    Max                 
};

EPeriodicEffect StringToPeriodicEffect(const std::string& v);
std::string PeriodicEffectToString(EPeriodicEffect v);

