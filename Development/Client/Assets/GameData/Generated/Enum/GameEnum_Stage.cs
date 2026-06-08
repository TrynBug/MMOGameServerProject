// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. GameEnum.xlsx 가 변경되면 재생성됩니다.
// =====================================================================

namespace GameData
{
    public enum EStageType
    {
        None                 = 0,
        System               = 1,  // 시스템
        Town                 = 2,  // 마을
        Field                = 3,  // 필드
        Dungeon              = 4,  // 던전
        Max                 
    }

    public enum EStagePositionType
    {
        None                 = 0,
        Default              = 1,  // 기본 시작위치
        Path1                = 2,  // 경로1
        Path2                = 3,  // 경로2
        Path3                = 4,  // 경로3
        Max                 
    }
}
