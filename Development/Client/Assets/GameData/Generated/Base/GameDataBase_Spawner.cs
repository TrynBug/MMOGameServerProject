// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

using System.Collections.Generic;

namespace GameData
{
    // Spawner 데이터 1개 행을 표현합니다.
    public class GameDataBase_Spawner : GameDataBase
    {
        public int                  Key                  = 0;
    }

    // Spawner 데이터 파일 전체를 표현합니다.
    public class GameDataTableBase_Spawner : GameDataTableBase
    {
        protected static Dictionary<int, GameData_Spawner> sm_dataMap = new();

        public override string GetDataName() => "Spawner";

        public static GameData_Spawner FindData(int key)
        {
            sm_dataMap.TryGetValue(key, out var data);
            return data;
        }

        public static IReadOnlyDictionary<int, GameData_Spawner> GetDataMap() => sm_dataMap;

        protected override bool MakeGameData(string line)
        {
            string[] fields = line.Split(',');
            GameData_Spawner data = new GameData_Spawner();

            data.Key = int.Parse(fields[0]);

            if (data.Key <= 0)
                return false;

            if (sm_dataMap.ContainsKey(data.Key))
                return false;

            if (!data.Initialize())
                return false;

            sm_dataMap[data.Key] = data;

            return OnAddData(data);
        }

        protected virtual bool OnAddData(GameData_Spawner data) => true;
    }
}
