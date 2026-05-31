// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

using System.Collections.Generic;

namespace GameData
{
    // Buff 데이터 1개 행을 표현합니다.
    public class GameDataBase_Buff : GameDataBase
    {
        public long                 Key                  = 0;
        public string               Name                 = "";
        public string               Desc                 = "";
        public EBuffCategory        Category             = EBuffCategory.None;
        public long                 DurationMs           = 0;
        public long                 MaxStack             = 1;
        public long                 IconId               = 0;
    }

    // Buff 데이터 파일 전체를 표현합니다.
    public class GameDataTableBase_Buff : GameDataTableBase
    {
        protected static Dictionary<long, GameData_Buff> sm_dataMap = new();

        public override string GetDataName() => "Buff";

        public static GameData_Buff FindData(long key)
        {
            sm_dataMap.TryGetValue(key, out var data);
            return data;
        }

        public static IReadOnlyDictionary<long, GameData_Buff> GetDataMap() => sm_dataMap;

        protected override bool MakeGameData(string line)
        {
            string[] fields = line.Split(',');
            GameData_Buff data = new GameData_Buff();

            data.Key = long.Parse(fields[0]);
            data.Name = fields[1];
            data.Desc = fields[2];
            data.Category = (EBuffCategory)int.Parse(fields[3]);
            data.DurationMs = long.Parse(fields[4]);
            data.MaxStack = long.Parse(fields[5]);
            data.IconId = long.Parse(fields[6]);

            if (data.Key <= 0)
                return false;

            if (sm_dataMap.ContainsKey(data.Key))
                return false;

            if (!data.Initialize())
                return false;

            sm_dataMap[data.Key] = data;

            return OnAddData(data);
        }

        protected virtual bool OnAddData(GameData_Buff data) => true;
    }
}
