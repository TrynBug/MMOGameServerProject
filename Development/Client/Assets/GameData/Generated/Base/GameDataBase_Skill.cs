// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

using System.Collections.Generic;

namespace GameData
{
    // Skill 데이터 1개 행을 표현합니다.
    public class GameDataBase_Skill : GameDataBase
    {
        public long                 Key                  = 0;
        public string               Name                 = "";
        public bool                 IsUserCastable       = false;
        public long                 NextSkillKey         = 0;
        public double               TriggerDelay         = 0;
        public EJob                 Job                  = EJob.None;
        public ESkillCategory       Category             = ESkillCategory.None;
        public double               CoolTime             = 0;
        public double               ManaCost             = 0;
        public double               CastTime             = 0;
        public bool                 NeedsTargeting       = true;
        public EEffectType          EffectType           = EEffectType.None;
        public double               Damage               = 0;
        public EOriginType          OriginType           = EOriginType.None;
        public ERangeShape          RangeShape           = ERangeShape.None;
        public double               RangeX               = 0;
        public double               RangeY               = 0;
        public double               IntervalSec          = 0;
        public double               FirstHitDelay        = 0;
        public double               DurationSec          = 0;
        public long                 ProjectileCount      = 0;
        public double               ProjectileSpreadAngle = 0;
        public double               ProjectileSpeed      = 0;
        public double               ProjectileRange      = 0;
        public long                 ProjectileMaxHitPerTarget = 0;
        public double               MoveDistance         = 0;
        public double               MoveTime             = 0;
        public double               MoveFastPhaseRatio   = 0;
        public double               MoveFastTimeRatio    = 0;
        public long                 MaxTargetCount       = 0;
        public double               KnockbackDistance    = 0;
        public long                 BuffKey              = 0;
        public long                 SlowOnHitKey         = 0;
    }

    // Skill 데이터 파일 전체를 표현합니다.
    public class GameDataTableBase_Skill : GameDataTableBase
    {
        protected static Dictionary<long, GameData_Skill> sm_dataMap = new();

        public override string GetDataName() => "Skill";

        public static GameData_Skill FindData(long key)
        {
            sm_dataMap.TryGetValue(key, out var data);
            return data;
        }

        public static IReadOnlyDictionary<long, GameData_Skill> GetDataMap() => sm_dataMap;

        protected override bool MakeGameData(string line)
        {
            string[] fields = line.Split(',');
            GameData_Skill data = new GameData_Skill();

            data.Key = long.Parse(fields[0]);
            data.Name = fields[1];
            data.IsUserCastable = GameDataTableBase.StringToBool(fields[2]);
            data.NextSkillKey = long.Parse(fields[3]);
            data.TriggerDelay = double.Parse(fields[4]);
            data.Job = (EJob)int.Parse(fields[5]);
            data.Category = (ESkillCategory)int.Parse(fields[6]);
            data.CoolTime = double.Parse(fields[7]);
            data.ManaCost = double.Parse(fields[8]);
            data.CastTime = double.Parse(fields[9]);
            data.NeedsTargeting = GameDataTableBase.StringToBool(fields[10]);
            data.EffectType = (EEffectType)int.Parse(fields[11]);
            data.Damage = double.Parse(fields[12]);
            data.OriginType = (EOriginType)int.Parse(fields[13]);
            data.RangeShape = (ERangeShape)int.Parse(fields[14]);
            data.RangeX = double.Parse(fields[15]);
            data.RangeY = double.Parse(fields[16]);
            data.IntervalSec = double.Parse(fields[17]);
            data.FirstHitDelay = double.Parse(fields[18]);
            data.DurationSec = double.Parse(fields[19]);
            data.ProjectileCount = long.Parse(fields[20]);
            data.ProjectileSpreadAngle = double.Parse(fields[21]);
            data.ProjectileSpeed = double.Parse(fields[22]);
            data.ProjectileRange = double.Parse(fields[23]);
            data.ProjectileMaxHitPerTarget = long.Parse(fields[24]);
            data.MoveDistance = double.Parse(fields[25]);
            data.MoveTime = double.Parse(fields[26]);
            data.MoveFastPhaseRatio = double.Parse(fields[27]);
            data.MoveFastTimeRatio = double.Parse(fields[28]);
            data.MaxTargetCount = long.Parse(fields[29]);
            data.KnockbackDistance = double.Parse(fields[30]);
            data.BuffKey = long.Parse(fields[31]);
            data.SlowOnHitKey = long.Parse(fields[32]);

            if (data.Key <= 0)
                return false;

            if (sm_dataMap.ContainsKey(data.Key))
                return false;

            if (!data.Initialize())
                return false;

            sm_dataMap[data.Key] = data;

            return OnAddData(data);
        }

        protected virtual bool OnAddData(GameData_Skill data) => true;
    }
}
