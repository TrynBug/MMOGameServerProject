#pragma once
// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. GameEnum.xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include <string>

enum class EStageType : int
{
    None                 = 0,
    System               = 1,  // 시스템
    Town                 = 2,  // 마을
    Field                = 3,  // 필드
    Dungeon              = 4,  // 던전
    Max                 
};

EStageType StringToStageType(const std::string& v);
std::string StageTypeToString(EStageType v);

