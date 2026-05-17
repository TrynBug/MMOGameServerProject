// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

using System.Collections.Generic;

public class GameDataBase_Monster
{
    public long                 Key = 0;
    public string               Name = "";
    public long                 HP = 100;
    public EMonsterGrade        Grade = EMonsterGrade.Normal;
    public EStat                Stat1 = EStat.None;
    public double               StatValue1 = 0;
    public EStat                Stat2 = EStat.None;
    public double               StatValue2 = 0;
    public EStat                Stat3 = EStat.None;
    public double               StatValue3 = 0;

    public int GetStatCount() => 3;
    public EStat GetStat(int index)
    {
        switch (index)
        {
            case 0: return Stat1;
            case 1: return Stat2;
            case 2: return Stat3;
            default: return EStat.None;
        }
    }

    public int GetStatValueCount() => 3;
    public double GetStatValue(int index)
    {
        switch (index)
        {
            case 0: return StatValue1;
            case 1: return StatValue2;
            case 2: return StatValue3;
            default: return 0;
        }
    }
}

public class GameDataTableBase_Monster
{
    protected static Dictionary<long, GameData_Monster> sm_dataMap = new();

    public static GameData_Monster? FindData(long key)
    {
        sm_dataMap.TryGetValue(key, out var data);
        return data;
    }

    public static IReadOnlyDictionary<long, GameData_Monster> GetDataMap() => sm_dataMap;

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
        GameData_Monster data = new GameData_Monster();

        data.Key = long.Parse(fields[0]);
        data.Name = fields[1];
        data.HP = long.Parse(fields[2]);
        data.Grade = (EMonsterGrade)int.Parse(fields[3]);
        data.Stat1 = (EStat)int.Parse(fields[4]);
        data.StatValue1 = double.Parse(fields[5]);
        data.Stat2 = (EStat)int.Parse(fields[6]);
        data.StatValue2 = double.Parse(fields[7]);
        data.Stat3 = (EStat)int.Parse(fields[8]);
        data.StatValue3 = double.Parse(fields[9]);

        if (data.Key <= 0)
            return false;

        if (sm_dataMap.ContainsKey(data.Key))
            return false;

        if (!data.Initialize())
            return false;

        sm_dataMap[data.Key] = data;

        return OnAddData(data);
    }

    protected virtual bool OnAddData(GameData_Monster data) => true;
    protected virtual bool OnLoadComplete() => true;
}
