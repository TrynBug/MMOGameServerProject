// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

using System.Collections.Generic;

namespace GameData
{
    // Item 데이터 1개 행을 표현합니다.
    public class GameDataBase_Item : GameDataBase
    {
        public int                  Key                  = 0;
        public string               Name                 = "";
        public int                  ItemType             = 0;
        public int                  Grade                = 0;
        public int                  MaxStack             = 1;
        public string               DropPrefabPath       = "";
    }

    // Item 데이터 파일 전체를 표현합니다.
    public class GameDataTableBase_Item : GameDataTableBase
    {
        protected static Dictionary<int, GameData_Item> sm_dataMap = new();

        public override string GetDataName() => "Item";

        public static GameData_Item FindData(int key)
        {
            sm_dataMap.TryGetValue(key, out var data);
            return data;
        }

        public static IReadOnlyDictionary<int, GameData_Item> GetDataMap() => sm_dataMap;

        protected override bool MakeGameData(string line)
        {
            string[] fields = line.Split(',');
            GameData_Item data = new GameData_Item();

            data.Key = int.Parse(fields[0]);
            data.Name = fields[1];
            data.ItemType = int.Parse(fields[2]);
            data.Grade = int.Parse(fields[3]);
            data.MaxStack = int.Parse(fields[4]);
            data.DropPrefabPath = fields[5];

            if (data.Key <= 0)
                return false;

            if (sm_dataMap.ContainsKey(data.Key))
                return false;

            if (!data.Initialize())
                return false;

            sm_dataMap[data.Key] = data;

            return OnAddData(data);
        }

        protected virtual bool OnAddData(GameData_Item data) => true;
    }
}
