// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

using System.Collections.Generic;

namespace GameData
{
    // Prop 데이터 1개 행을 표현합니다.
    public class GameDataBase_Prop : GameDataBase
    {
        public int                  Key                  = 0;
        public string               PrefabPath           = "";
    }

    // Prop 데이터 파일 전체를 표현합니다.
    public class GameDataTableBase_Prop : GameDataTableBase
    {
        protected static Dictionary<int, GameData_Prop> sm_dataMap = new();

        public override string GetDataName() => "Prop";

        public static GameData_Prop FindData(int key)
        {
            sm_dataMap.TryGetValue(key, out var data);
            return data;
        }

        public static IReadOnlyDictionary<int, GameData_Prop> GetDataMap() => sm_dataMap;

        protected override bool MakeGameData(string line)
        {
            string[] fields = line.Split(',');
            GameData_Prop data = new GameData_Prop();

            data.Key = int.Parse(fields[0]);
            data.PrefabPath = fields[1];

            if (data.Key <= 0)
                return false;

            if (sm_dataMap.ContainsKey(data.Key))
                return false;

            if (!data.Initialize())
                return false;

            sm_dataMap[data.Key] = data;

            return OnAddData(data);
        }

        protected virtual bool OnAddData(GameData_Prop data) => true;
    }
}
