// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. GameEnum.xlsx 가 변경되면 재생성됩니다.
// =====================================================================

namespace GameData
{
    public enum ESkillCastClass
    {
        None                 = 0,
        Stationary           = 1,  // 제자리시전
        Mobile               = 2,  // 이동시전
        Mobility             = 3,  // 이동기
        Max                 
    }

    public enum ENextSkillOrigin
    {
        None                 = 0,
        CasterPos            = 1,  // 캐스터 현재 위치
        CasterFront          = 2,  // 캐스터 정면 + CasterFrontDistance
        PrevCenter           = 3,  // 이전 페이즈 중심
        PrevEnd              = 4,  // 이전 페이즈 종료 위치
        Max                 
    }

    public enum ENextSkillTiming
    {
        None                 = 0,
        OnStart              = 1,  // 이전 스킬 발동과 동시
        AfterEnd             = 2,  // 이전 스킬 lifetime 종료 직후
        AfterDelay           = 3,  // 이전 스킬 litetime 종료 + NextTriggerDelayMs
        Max                 
    }

    public enum ESkillEffectMotion
    {
        None                 = 0,
        Static               = 1,  // 고정 (얼음지대, 화염지대, 전격방출, 메테오 착탄 등)
        Linear               = 2,  // 직선 등속 (이동하는 장판 = 하이브리드)
        Max                 
    }

    public enum ESkillEffectDamage
    {
        None                 = 0,
        ContactHit           = 1,  // 투사체 접촉 시 1회. 클라 hit 보고 기반 → ProjectileGroup
        Area                 = 2,  // 서버 주도 범위 틱 → AreaEffect. 틱 횟수/간격이 instant(1회)/periodic(N회) 을 결정.
        Max                 
    }

    public enum ESkillEffectShape
    {
        None                 = 0,
        Circle               = 1,  // 원 (전격방출, 메테오 착탄, 파이어볼 폭발 등)
        Obb                  = 2,  // 방향이 있는 직사각형, Oriented Bounding Box (얼음지대, 블레이즈)
        Max                 
    }
}
