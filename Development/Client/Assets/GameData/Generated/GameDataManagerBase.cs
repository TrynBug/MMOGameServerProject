// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다. 직접 수정하지 마세요.
// =====================================================================

public class GameDataManagerBase
{
    public static bool LoadAllGameData(string csvPath)
    {
        var table_Monster = new GameDataTable_Monster();
        if (!table_Monster.LoadData(System.IO.Path.Combine(csvPath, "Monster.csv")))
            return false;

        var table_Stage = new GameDataTable_Stage();
        if (!table_Stage.LoadData(System.IO.Path.Combine(csvPath, "Stage.csv")))
            return false;

        return true;
    }
}
