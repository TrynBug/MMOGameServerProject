// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. GameEnum.xlsx 가 변경되면 재생성됩니다.
// =====================================================================

namespace GameData
{
    public enum ETeam
    {
        None                 = 0,
        User                 = 1,  // 유저
        Monster              = 2,  // 몬스터
        Max                 
    }

    public enum EJob
    {
        None                 = 0,
        Mage                 = 1,  // 마법사
        Warrior              = 2,  // 전사
        Max                 
    }

    public enum EObjectType
    {
        None                 = 0,
        User                 = 1,  // 유저
        Monster              = 2,  // 몬스터
        Prop                 = 3,  // 프랍
        Drop                 = 4,  // 드롭아이템
        Max                 
    }

    public enum EResultCode
    {
        None                 = 0,
        Success              = 1,  // 성공
        Fail                 = 2,  // 실패
        Max                 
    }
}
