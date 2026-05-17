// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

using System.Collections.Generic;

public class GameDataBase_Stage
{
    public long                 Key = 0;
    public string               Name = "";
    public EStageType           StageType = EStageType.None;
}

public class GameDataTableBase_Stage
{
    protected static Dictionary<long, GameData_Stage> sm_dataMap = new();

    public static GameData_Stage? FindData(long key)
    {
        sm_dataMap.TryGetValue(key, out var data);
        return data;
    }

    public static IReadOnlyDictionary<long, GameData_Stage> GetDataMap() => sm_dataMap;

    public bool LoadData(string csvPath)
    {
        sm_dataMap.Clear();

        string[] lines = File.ReadAllLines(csvPath);
        if (lines.Length < 2)
            return true;

        // 첫 번째 행은 헤더이므로 건너뜀
        for (int i = 1; i < lines.Length; i++)
        {
            string line = lines[i].Trim();
            if (string.IsNullOrEmpty(line))
                continue;

            if (!MakeGameData(line))
                return false;
        }

        return OnLoadComplete();
    }

    protected bool MakeGameData(string line)
    {
        string[] fields = line.Split(',');
        GameData_Stage data = new GameData_Stage();

        data.Key = long.Parse(fields[0]);
        data.Name = fields[1];
        data.StageType = (EStageType)int.Parse(fields[2]);

        if (data.Key <= 0)
            return false;

        if (sm_dataMap.ContainsKey(data.Key))
            return false;

        if (!data.Initialize())
            return false;

        sm_dataMap[data.Key] = data;

        return OnAddData(data);
    }

    protected virtual bool OnAddData(GameData_Stage data) => true;
    protected virtual bool OnLoadComplete() => true;
}
