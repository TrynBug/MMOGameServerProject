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

enum class EJob : int
{
    None                 = 0,
    Wizard               = 1,  // 마법사
    Warrior              = 2,  // 전사
    Max                 
};

EJob StringToJob(const std::string& v);
std::string JobToString(EJob v);

enum class EObjectType : int
{
    None                 = 0,
    User                 = 1,  // 유저
    Monster              = 2,  // 몬스터
    Prop                 = 3,  // 프랍
    Drop                 = 4,  // 드롭아이템
    Max                 
};

EObjectType StringToObjectType(const std::string& v);
std::string ObjectTypeToString(EObjectType v);

enum class EResultCode : int
{
    None                 = 0,
    Success              = 1,  // 성공
    Fail                 = 2,  // 실패
    Max                 
};

EResultCode StringToResultCode(const std::string& v);
std::string ResultCodeToString(EResultCode v);

