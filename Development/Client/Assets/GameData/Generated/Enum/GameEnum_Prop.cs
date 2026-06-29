// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. GameEnum.xlsx 가 변경되면 재생성됩니다.
// =====================================================================

namespace GameData
{
    /* prop 상태머신 모드 */
    public enum EPropStateMode
    {
        None                 = 0,
        Toggle               = 1,  // 토글    // 상호작용마다 상태 순환(StateCount 만큼)
        OneShot              = 2,  // 일회성    // 0→1 1회 전이 후 더 이상 상태변화 없음
        Max                 
    }

    /* prop 선언형 동작 */
    public enum EPropBehavior
    {
        None                 = 0,
        Portal               = 1,  // 포탈    // 상호작용 시 지정 스테이지/위치로 이동(무스크립트)
        Max                 
    }
}
