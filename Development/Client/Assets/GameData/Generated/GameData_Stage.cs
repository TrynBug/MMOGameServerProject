// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 최초 1회만 생성됩니다.
// 이후에는 사용자가 직접 수정할 수 있습니다.
// =====================================================================

public class GameData_Stage : GameDataBase_Stage
{
    public bool Initialize()
    {
        // 데이터 1건을 읽은 직후 호출됨
        return true;
    }

    // 여기에 사용자가 추가할 필드, 메서드를 선언합니다.
}

public class GameDataTable_Stage : GameDataTableBase_Stage
{
    protected override bool OnAddData(GameData_Stage data)
    {
        // 데이터가 sm_dataMap 에 추가된 후 호출됨
        return true;
    }

    protected override bool OnLoadComplete()
    {
        // 전체 데이터 로드 완료 후 호출됨
        // 예: 다른 테이블과의 참조 연결, 유효성 검증 등
        return true;
    }

    // 여기에 사용자가 추가할 멤버함수를 선언합니다.
}
