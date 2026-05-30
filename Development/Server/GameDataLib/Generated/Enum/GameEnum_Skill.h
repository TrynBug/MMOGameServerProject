#pragma once
// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. GameEnum.xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include <string>

enum class ESkillCategory : int
{
    None                 = 0,
    StationaryCast       = 1,  // 제자리시전
    MobileCast           = 2,  // 이동시전
    Movement             = 3,  // 이동기
    Max                 
};

ESkillCategory StringToSkillCategory(const std::string& v);
std::string SkillCategoryToString(ESkillCategory v);

enum class EEffectType : int
{
    None                 = 0,
    Movement             = 1,  // 강제이동
    Projectile           = 2,  // 투사체
    InstantDamage        = 3,  // 즉시타격
    TickDamageArea       = 4,  // 지속타격영역
    Buff                 = 5,  // 버프
    VFX                  = 6,  // 시각효과
    Max                 
};

EEffectType StringToEffectType(const std::string& v);
std::string EffectTypeToString(EEffectType v);

enum class ERangeShape : int
{
    None                 = 0,
    Circle               = 1,  // 원형
    Rectangle            = 2,  // 사각형
    Sector               = 3,  // 부채꼴
    Max                 
};

ERangeShape StringToRangeShape(const std::string& v);
std::string RangeShapeToString(ERangeShape v);

enum class EOriginType : int
{
    None                 = 0,
    CasterCenter         = 1,  // 캐릭터 위치 중심
    TargetCenter         = 2,  // 타겟 위치 중심
    CasterForward        = 3,  // 캐릭터 위치에서 전방으로 뻗음
    Max                 
};

EOriginType StringToOriginType(const std::string& v);
std::string OriginTypeToString(EOriginType v);

