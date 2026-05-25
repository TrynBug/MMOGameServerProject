// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

using System.Collections.Generic;

namespace GameData
{
    // Monster 데이터 1개 행을 표현합니다.
    public class GameDataBase_Monster : GameDataBase
    {
        public long                 Key                  = 0;
        public string               Name                 = "";
        public long                 HP                   = 100;
        public EMonsterGrade        Grade                = EMonsterGrade.Normal;
        public EStat                Stat1                = EStat.None;
        public double               StatValue1           = 0;
        public EStat                Stat2                = EStat.None;
        public double               StatValue2           = 0;
        public EStat                Stat3                = EStat.None;
        public double               StatValue3           = 0;

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

    // Monster 데이터 파일 전체를 표현합니다.
    public class GameDataTableBase_Monster : GameDataTableBase
    {
        protected static Dictionary<long, GameData_Monster> sm_dataMap = new();

        public override string GetDataName() => "Monster";

        public static GameData_Monster FindData(long key)
        {
            sm_dataMap.TryGetValue(key, out var data);
            return data;
        }

        public static IReadOnlyDictionary<long, GameData_Monster> GetDataMap() => sm_dataMap;

        protected override bool MakeGameData(string line)
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
    }
}
