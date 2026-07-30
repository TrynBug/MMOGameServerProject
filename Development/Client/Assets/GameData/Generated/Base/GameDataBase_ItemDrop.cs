// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

using System.Collections.Generic;

namespace GameData
{
    // ItemDrop 데이터 1개 행을 표현합니다.
    public class GameDataBase_ItemDrop : GameDataBase
    {
        public int                  Key                  = 0;
        public int                  GroupKey             = 0;
        public int                  ItemKey              = 0;
        public int                  ChancePermyriad      = 0;
        public int                  MinCount             = 1;
        public int                  MaxCount             = 1;
    }

    // ItemDrop 데이터 파일 전체를 표현합니다.
    public class GameDataTableBase_ItemDrop : GameDataTableBase
    {
        protected static Dictionary<int, GameData_ItemDrop> sm_dataMap = new();

        public override string GetDataName() => "ItemDrop";

        public static GameData_ItemDrop FindData(int key)
        {
            sm_dataMap.TryGetValue(key, out var data);
            return data;
        }

        public static IReadOnlyDictionary<int, GameData_ItemDrop> GetDataMap() => sm_dataMap;

        protected override bool MakeGameData(string line)
        {
            string[] fields = line.Split(',');
            GameData_ItemDrop data = new GameData_ItemDrop();

            data.Key = int.Parse(fields[0]);
            data.GroupKey = int.Parse(fields[1]);
            data.ItemKey = int.Parse(fields[2]);
            data.ChancePermyriad = int.Parse(fields[3]);
            data.MinCount = int.Parse(fields[4]);
            data.MaxCount = int.Parse(fields[5]);

            if (data.Key <= 0)
                return false;

            if (sm_dataMap.ContainsKey(data.Key))
                return false;

            if (!data.Initialize())
                return false;

            sm_dataMap[data.Key] = data;

            return OnAddData(data);
        }

        protected virtual bool OnAddData(GameData_ItemDrop data) => true;
    }
}
