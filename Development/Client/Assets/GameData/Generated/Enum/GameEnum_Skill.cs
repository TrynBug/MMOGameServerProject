// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. GameEnum.xlsx 가 변경되면 재생성됩니다.
// =====================================================================

namespace GameData
{
    public enum ESkillCastClass
    {
        None                 = 0,
        Stationary           = 1,  // 제자리시전    // 스킬 사용중 움직일 수 없음
        Mobile               = 2,  // 이동시전    // 스킬 사용중 움직일 수 있음
        Mobility             = 3,  // 이동기    // 이동전용 스킬
        Max                 
    }

    /* 다음페이즈 스킬이 트리거되는 기준 위치 */
    public enum ENextSkillOrigin
    {
        None                 = 0,
        CasterPos            = 1,  // 캐스터 현재 위치
        CasterFront          = 2,  // 캐스터 정면 + CasterFrontDistance
        PrevCenter           = 3,  // 이전 페이즈 중심
        PrevEnd              = 4,  // 이전 페이즈 종료 위치
        Max                 
    }

    /* 다음페이즈 스킬이 트리거되는 타이밍 */
    public enum ENextSkillTiming
    {
        None                 = 0,
        OnStart              = 1,  // 이전 스킬 발동과 동시
        AfterEnd             = 2,  // 이전 스킬 lifetime 종료 직후
        AfterDelay           = 3,  // 이전 스킬 litetime 종료 + NextTriggerDelayMs
        Max                 
    }

    /* 스킬 효과의 위치가 시간에 따라 어떻게 변하는지 결정 */
    public enum ESkillEffectMotion
    {
        None                 = 0,
        Static               = 1,  // 고정    // 얼음지대, 화염지대, 전격방출, 메테오 착탄 등
        Linear               = 2,  // 직선 등속    // 이동하는 장판 등의 하이브리드
        Max                 
    }

    /* 스킬 대미지를 어떻게 주는지 결정 */
    public enum ESkillEffectDamage
    {
        None                 = 0,
        ContactHit           = 1,  // 투사체 접촉 시 1회    // 투사체 접촉 시 클라이언트가 hit을 보고한다.
        Area                 = 2,  // 서버 주도 범위 틱    // 서버에서 영역내의 적에게 대미지를 입힌다.
        Max                 
    }

    /* EffectShape — 스킬/효과의 범위 모양 (X-Z 평면)
범위 판정은 모두 X-Z 평면에서 한다 (높이 Y 는 무시). 게임이 쿼터뷰 평면 기반이라 대미지 범위는 평면 모양으로 충분하다.
중심 좌표(center)는 모양에 넣지 않는다. 이동하는 효과는 매 tick center 가 달라지므로 center 는 판정 시점에 인자로 받고, 모양 자체(반지름/크기/방향)만 보관한다. */
    public enum ESkillEffectShape
    {
        None                 = 0,
        Circle               = 1,  // 원형    // 전격방출, 메테오 착탄, 파이어볼 폭발 등
        Obb                  = 2,  // 방향이 있는 직사각형, OBB(Oriented Bounding Box)    // 얼음지대, 블레이즈
        Max                 
    }

    /* 스킬의 타게팅 방식 */
    public enum ETargetingMode
    {
        None                 = 0,
        Nearest              = 1,  // 가장 가까운 타겟
        HighestGradeNearest  = 2,  // 가장 등급이 높으면서 가장 가까운 타겟
        Max                 
    }

    /* 스킬 효과 중심의 기준점을 정한다. */
    public enum ESkillPlacement
    {
        None                 = 0,
        Caster               = 1,  // 캐스터 중심    // 시전자 루트/중심을 효과 중심 기준점으로 사용
        SkillCastOrigin      = 2,  // 스킬 시전 앵커    // 시전자 프리팹의 SkillCastOrigin을 효과 중심 기준점으로 사용
        Target               = 3,  // 타겟 위치    // 타겟 위치를 효과 중심 기준점으로 사용
        Max                 
    }
}
