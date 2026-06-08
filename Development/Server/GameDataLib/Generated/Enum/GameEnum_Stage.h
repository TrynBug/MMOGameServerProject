#pragma once
// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. GameEnum.xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include <string>

enum class EStageType : int
{
    None                 = 0,
    System               = 1,  // 시스템    // 캐릭터 선택창 등의 시스템 스테이지
    Town                 = 2,  // 마을
    Field                = 3,  // 필드
    Dungeon              = 4,  // 던전
    Max                 
};

EStageType StringToStageType(const std::string& v);
std::string StageTypeToString(EStageType v);

enum class EStagePositionType : int
{
    None                 = 0,
    Default              = 1,  // 기본 시작위치    // 캐릭터 선택 입장 등 기본 도착 위치
    Path1                = 2,  // 경로1    // 외부에서 들어오는 첫번째 위치
    Path2                = 3,  // 경로2    // 외부에서 들어오는 두번째 위치
    Path3                = 4,  // 경로3    // 외부에서 들어오는 세번째 위치
    Max                 
};

EStagePositionType StringToStagePositionType(const std::string& v);
std::string StagePositionTypeToString(EStagePositionType v);

