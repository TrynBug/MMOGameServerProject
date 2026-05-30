// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다. 직접 수정하지 마세요.
// =====================================================================

namespace GameData
{
    public class GameDataManagerBase
    {
        public static bool LoadAllGameData(string csvPath)
        {
            var table_JobBase = new GameDataTable_JobBase();
            if (!table_JobBase.LoadData(csvPath))
                return false;

            var table_Monster = new GameDataTable_Monster();
            if (!table_Monster.LoadData(csvPath))
                return false;

            var table_Skill = new GameDataTable_Skill();
            if (!table_Skill.LoadData(csvPath))
                return false;

            var table_Stage = new GameDataTable_Stage();
            if (!table_Stage.LoadData(csvPath))
                return false;

            var table_Stat = new GameDataTable_Stat();
            if (!table_Stat.LoadData(csvPath))
                return false;

            return true;
        }
    }
}
