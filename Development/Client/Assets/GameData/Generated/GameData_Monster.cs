// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 최초 1회만 생성됩니다.
// 이후에는 사용자가 직접 수정할 수 있습니다.
// =====================================================================

public class GameData_Monster : GameDataBase_Monster
{
    // 여기에 사용자가 추가할 필드를 선언합니다.

    public bool Initialize()
    {
        // 여기에 데이터 1건에 대한 사용자 초기화 로직을 작성합니다.
        return true;
    }
}

public class GameDataTable_Monster : GameDataTableBase_Monster
{
    // 여기에 사용자가 추가할 멤버함수를 선언합니다.

    protected override bool OnAddData(GameData_Monster data)
    {
        if (!data.Initialize())
            return false;
        // 여기에 데이터 1건이 추가될 때 사용자 로직을 작성합니다.
        return true;
    }

    protected override bool OnLoadComplete()
    {
        // 여기에 전체 데이터 로드 완료 후 사용자 로직을 작성합니다.
        return true;
    }
}
