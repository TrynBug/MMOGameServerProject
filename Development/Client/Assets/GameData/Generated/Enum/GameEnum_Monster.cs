// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. GameEnum.xlsx 가 변경되면 재생성됩니다.
// =====================================================================

namespace GameData
{
    public enum EMonsterGrade
    {
        None                 = 0,
        Normal               = 1,  // 일반
        Magic                = 2,  // 매직
        Rare                 = 3,  // 레어
        Unique               = 4,  // 유니크
        Boss                 = 5,  // 보스
        Max                 
    }

    /* 몬스터 AI 종류 */
    public enum EMonsterAIType
    {
        None                 = 0,
        FSM                  = 1,  // 유한상태기계    // 일반몬스터용
        BehaviourTree        = 2,  // 행동 트리    // 엘리트 몬스터, 보스용
        Max                 
    }

    /* 스폰 활성화 모드 */
    public enum ESpawnActivation
    {
        None                 = 0,
        Always               = 1,  // 항상    // 항상 밀도 유지
        PlayerProximity      = 2,  // 근접 활성    // 플레이어 근접 시만
        Manual               = 3,  // 수동    // Stage 스크립트에서 ActivateSpawner 등으로 활성화시킴
        Max                 
    }
}
