// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

using System.Collections.Generic;

namespace GameData
{
    // Stat 데이터 1개 행을 표현합니다.
    public class GameDataBase_Stat : GameDataBase
    {
        public long                 Key                  = 0;
        public EStat                Stat                 = EStat.None;
        public EStatGroup           StatGroup            = EStatGroup.None;
        public EStatOp              StatOp               = EStatOp.None;
        public double               MinApplyValue        = -1000000000;
        public double               MaxApplyValue        = 1000000000;
    }

    // Stat 데이터 파일 전체를 표현합니다.
    public class GameDataTableBase_Stat : GameDataTableBase
    {
        protected static Dictionary<long, GameData_Stat> sm_dataMap = new();

        public override string GetDataName() => "Stat";

        public static GameData_Stat FindData(long key)
        {
            sm_dataMap.TryGetValue(key, out var data);
            return data;
        }

        public static IReadOnlyDictionary<long, GameData_Stat> GetDataMap() => sm_dataMap;

        protected override bool MakeGameData(string line)
        {
            string[] fields = line.Split(',');
            GameData_Stat data = new GameData_Stat();

            data.Key = long.Parse(fields[0]);
            data.Stat = (EStat)int.Parse(fields[1]);
            data.StatGroup = (EStatGroup)int.Parse(fields[2]);
            data.StatOp = (EStatOp)int.Parse(fields[3]);
            data.MinApplyValue = double.Parse(fields[4]);
            data.MaxApplyValue = double.Parse(fields[5]);

            if (data.Key <= 0)
                return false;

            if (sm_dataMap.ContainsKey(data.Key))
                return false;

            if (!data.Initialize())
                return false;

            sm_dataMap[data.Key] = data;

            return OnAddData(data);
        }

        protected virtual bool OnAddData(GameData_Stat data) => true;
    }
}
