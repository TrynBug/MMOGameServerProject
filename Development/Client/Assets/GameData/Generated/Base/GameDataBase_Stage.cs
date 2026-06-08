// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

using System.Collections.Generic;

namespace GameData
{
    // Stage 데이터 1개 행을 표현합니다.
    public class GameDataBase_Stage : GameDataBase
    {
        public int                  Key                  = 0;
        public string               Name                 = "";
        public EStageType           StageType            = EStageType.None;
        public string               StagePrefabPath      = "";
        public string               NavMeshFileName      = "";
        public float                sectorSize           = 0f;
    }

    // Stage 데이터 파일 전체를 표현합니다.
    public class GameDataTableBase_Stage : GameDataTableBase
    {
        protected static Dictionary<int, GameData_Stage> sm_dataMap = new();

        public override string GetDataName() => "Stage";

        public static GameData_Stage FindData(int key)
        {
            sm_dataMap.TryGetValue(key, out var data);
            return data;
        }

        public static IReadOnlyDictionary<int, GameData_Stage> GetDataMap() => sm_dataMap;

        protected override bool MakeGameData(string line)
        {
            string[] fields = line.Split(',');
            GameData_Stage data = new GameData_Stage();

            data.Key = int.Parse(fields[0]);
            data.Name = fields[1];
            data.StageType = (EStageType)int.Parse(fields[2]);
            data.StagePrefabPath = fields[3];
            data.NavMeshFileName = fields[4];
            data.sectorSize = float.Parse(fields[5]);

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
    }
}
