// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

using System.Collections.Generic;

namespace GameData
{
    // JobBase 데이터 1개 행을 표현합니다.
    public class GameDataBase_JobBase : GameDataBase
    {
        public int                  Key                  = 0;
        public EJob                 Job                  = EJob.None;
        public EStat                Stat1                = EStat.None;
        public double               StatValue1           = 0;
        public EStat                Stat2                = EStat.None;
        public double               StatValue2           = 0;
        public EStat                Stat3                = EStat.None;
        public double               StatValue3           = 0;
        public EStat                Stat4                = EStat.None;
        public double               StatValue4           = 0;
        public EStat                Stat5                = EStat.None;
        public double               StatValue5           = 0;
        public EStat                Stat6                = EStat.None;
        public double               StatValue6           = 0;
        public EStat                Stat7                = EStat.None;
        public double               StatValue7           = 0;
        public EStat                Stat8                = EStat.None;
        public double               StatValue8           = 0;

        public int GetStatCount() => 8;
        public EStat GetStat(int index)
        {
            switch (index)
            {
                case 0: return Stat1;
                case 1: return Stat2;
                case 2: return Stat3;
                case 3: return Stat4;
                case 4: return Stat5;
                case 5: return Stat6;
                case 6: return Stat7;
                case 7: return Stat8;
                default: return EStat.None;
            }
        }

        public int GetStatValueCount() => 8;
        public double GetStatValue(int index)
        {
            switch (index)
            {
                case 0: return StatValue1;
                case 1: return StatValue2;
                case 2: return StatValue3;
                case 3: return StatValue4;
                case 4: return StatValue5;
                case 5: return StatValue6;
                case 6: return StatValue7;
                case 7: return StatValue8;
                default: return 0;
            }
        }
    }

    // JobBase 데이터 파일 전체를 표현합니다.
    public class GameDataTableBase_JobBase : GameDataTableBase
    {
        protected static Dictionary<int, GameData_JobBase> sm_dataMap = new();

        public override string GetDataName() => "JobBase";

        public static GameData_JobBase FindData(int key)
        {
            sm_dataMap.TryGetValue(key, out var data);
            return data;
        }

        public static IReadOnlyDictionary<int, GameData_JobBase> GetDataMap() => sm_dataMap;

        protected override bool MakeGameData(string line)
        {
            string[] fields = line.Split(',');
            GameData_JobBase data = new GameData_JobBase();

            data.Key = int.Parse(fields[0]);
            data.Job = (EJob)int.Parse(fields[1]);
            data.Stat1 = (EStat)int.Parse(fields[2]);
            data.StatValue1 = double.Parse(fields[3]);
            data.Stat2 = (EStat)int.Parse(fields[4]);
            data.StatValue2 = double.Parse(fields[5]);
            data.Stat3 = (EStat)int.Parse(fields[6]);
            data.StatValue3 = double.Parse(fields[7]);
            data.Stat4 = (EStat)int.Parse(fields[8]);
            data.StatValue4 = double.Parse(fields[9]);
            data.Stat5 = (EStat)int.Parse(fields[10]);
            data.StatValue5 = double.Parse(fields[11]);
            data.Stat6 = (EStat)int.Parse(fields[12]);
            data.StatValue6 = double.Parse(fields[13]);
            data.Stat7 = (EStat)int.Parse(fields[14]);
            data.StatValue7 = double.Parse(fields[15]);
            data.Stat8 = (EStat)int.Parse(fields[16]);
            data.StatValue8 = double.Parse(fields[17]);

            if (data.Key <= 0)
                return false;

            if (sm_dataMap.ContainsKey(data.Key))
                return false;

            if (!data.Initialize())
                return false;

            sm_dataMap[data.Key] = data;

            return OnAddData(data);
        }

        protected virtual bool OnAddData(GameData_JobBase data) => true;
    }
}
