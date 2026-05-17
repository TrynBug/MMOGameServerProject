#pragma once
// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. GameEnum.xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include <string>

enum class ETeam : int
{
    None                 = 0,
    User                 = 1,  // 유저
    Monster              = 2,  // 몬스터
    Max                 
};

ETeam StringToTeam(const std::string& v);
std::string TeamToString(ETeam v);

