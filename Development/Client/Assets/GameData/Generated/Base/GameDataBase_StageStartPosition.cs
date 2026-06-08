// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

using System.Collections.Generic;

namespace GameData
{
    // StageStartPosition 데이터 1개 행을 표현합니다.
    public class GameDataBase_StageStartPosition : GameDataBase
    {
        public int                  Key                  = 0;
        public int                  StageKey             = 0;
        public EStagePositionType   StagePositionType    = EStagePositionType.None;
        public float                PosX                 = 0f;
        public float                PosY                 = 0f;
        public float                PosZ                 = 0f;
        public float                Yaw                  = 0f;
    }

    // StageStartPosition 데이터 파일 전체를 표현합니다.
    public class GameDataTableBase_StageStartPosition : GameDataTableBase
    {
        protected static Dictionary<int, GameData_StageStartPosition> sm_dataMap = new();

        public override string GetDataName() => "StageStartPosition";

        public static GameData_StageStartPosition FindData(int key)
        {
            sm_dataMap.TryGetValue(key, out var data);
            return data;
        }

        public static IReadOnlyDictionary<int, GameData_StageStartPosition> GetDataMap() => sm_dataMap;

        protected override bool MakeGameData(string line)
        {
            string[] fields = line.Split(',');
            GameData_StageStartPosition data = new GameData_StageStartPosition();

            data.Key = int.Parse(fields[0]);
            data.StageKey = int.Parse(fields[1]);
            data.StagePositionType = (EStagePositionType)int.Parse(fields[2]);
            data.PosX = float.Parse(fields[3]);
            data.PosY = float.Parse(fields[4]);
            data.PosZ = float.Parse(fields[5]);
            data.Yaw = float.Parse(fields[6]);

            if (data.Key <= 0)
                return false;

            if (sm_dataMap.ContainsKey(data.Key))
                return false;

            if (!data.Initialize())
                return false;

            sm_dataMap[data.Key] = data;

            return OnAddData(data);
        }

        protected virtual bool OnAddData(GameData_StageStartPosition data) => true;
    }
}
