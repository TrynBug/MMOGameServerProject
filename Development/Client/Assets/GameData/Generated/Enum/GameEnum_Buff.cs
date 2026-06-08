// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. GameEnum.xlsx 가 변경되면 재생성됩니다.
// =====================================================================

namespace GameData
{
    public enum EBuffCategory
    {
        None                 = 0,
        Buff                 = 1,  // 이로운효과
        Debuff               = 2,  // 해로운효과    // 디스펠 대상
        Max                 
    }

    public enum EBuffStackPolicy
    {
        None                 = 0,
        Refresh              = 1,  // 시간갱신    // 재적용 시 지속시간만 리셋
        Stack                = 2,  // 스택증가    // 재적용 시 스택+1 (MaxStack까지)
        Ignore               = 3,  // 무시    // 이미 있으면 무시
        Max                 
    }

    /* 버프의 주기적 효과 */
    public enum EPeriodicEffect
    {
        None                 = 0,
        DamageHp             = 1,  // HP감소    // DoT
        HealHp               = 2,  // HP회복    // HoT
        Max                 
    }
}
