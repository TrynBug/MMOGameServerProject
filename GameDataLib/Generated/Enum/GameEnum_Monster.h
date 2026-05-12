#pragma once
// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. GameEnum.xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include <string>

enum class EMonsterGrade : int
{
    None                 = 0,
    Normal               = 1,  // 일반
    Magic                = 2,  // 매직
    Rare                 = 3,  // 레어
    Boss                 = 4,  // 보스
    Max                 
};

EMonsterGrade StringToMonsterGrade(const std::string& v);
std::string MonsterGradeToString(EMonsterGrade v);

