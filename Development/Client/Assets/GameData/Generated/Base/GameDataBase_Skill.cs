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
        public string               ProjectilePrefabPath = "";
        public string               CastAnim             = "";
        public EJob                 Job                  = EJob.None;
        public ESkillCastClass      CastClass            = ESkillCastClass.None;
        public bool                 IsPrimaryEligible    = false;
        public long                 OnHitSkillKey        = 0;
        public long                 NextSkillKey         = 0;
        public ENextSkillTiming     NextTriggerTiming    = ENextSkillTiming.None;
        public long                 NextTriggerDelayMs   = 0;
        public ENextSkillOrigin     NextOrigin           = ENextSkillOrigin.None;
        public double               CasterFrontDistance  = 0;
        public long                 CooldownMs           = 0;
        public double               ManaCost             = 0;
        public long                 CastDelayMs          = 0;
        public long                 ActionLockMs         = 0;
        public bool                 Rotation             = true;
        public ESkillEffectMotion   EffectMotion         = ESkillEffectMotion.None;
        public ESkillEffectDamage   EffectDamage         = ESkillEffectDamage.None;
        public ESkillEffectShape    EffectShape          = ESkillEffectShape.None;
        public double               DamageCoeff          = 0;
        public double               Radius               = 0;
        public double               ObbWidth             = 0;
        public double               ObbLength            = 0;
        public double               ProjectileSpeed      = 0;
        public double               MaxRange             = 0;
        public long                 ProjectileCount      = 0;
        public double               FanAngleDeg          = 0;
        public long                 FirstTickDelayMs     = 0;
        public long                 TickIntervalMs       = 0;
        public long                 LifetimeMs           = 0;
        public double               MoveDistance         = 0;
        public long                 MoveDurationMs       = 0;
        public long                 ScatterCount         = 0;
        public double               ScatterInnerRadius   = 0;
        public double               ScatterOuterRadius   = 0;
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
            data.ProjectilePrefabPath = fields[2];
            data.CastAnim = fields[3];
            data.Job = (EJob)int.Parse(fields[4]);
            data.CastClass = (ESkillCastClass)int.Parse(fields[5]);
            data.IsPrimaryEligible = GameDataTableBase.StringToBool(fields[6]);
            data.OnHitSkillKey = long.Parse(fields[7]);
            data.NextSkillKey = long.Parse(fields[8]);
            data.NextTriggerTiming = (ENextSkillTiming)int.Parse(fields[9]);
            data.NextTriggerDelayMs = long.Parse(fields[10]);
            data.NextOrigin = (ENextSkillOrigin)int.Parse(fields[11]);
            data.CasterFrontDistance = double.Parse(fields[12]);
            data.CooldownMs = long.Parse(fields[13]);
            data.ManaCost = double.Parse(fields[14]);
            data.CastDelayMs = long.Parse(fields[15]);
            data.ActionLockMs = long.Parse(fields[16]);
            data.Rotation = GameDataTableBase.StringToBool(fields[17]);
            data.EffectMotion = (ESkillEffectMotion)int.Parse(fields[18]);
            data.EffectDamage = (ESkillEffectDamage)int.Parse(fields[19]);
            data.EffectShape = (ESkillEffectShape)int.Parse(fields[20]);
            data.DamageCoeff = double.Parse(fields[21]);
            data.Radius = double.Parse(fields[22]);
            data.ObbWidth = double.Parse(fields[23]);
            data.ObbLength = double.Parse(fields[24]);
            data.ProjectileSpeed = double.Parse(fields[25]);
            data.MaxRange = double.Parse(fields[26]);
            data.ProjectileCount = long.Parse(fields[27]);
            data.FanAngleDeg = double.Parse(fields[28]);
            data.FirstTickDelayMs = long.Parse(fields[29]);
            data.TickIntervalMs = long.Parse(fields[30]);
            data.LifetimeMs = long.Parse(fields[31]);
            data.MoveDistance = double.Parse(fields[32]);
            data.MoveDurationMs = long.Parse(fields[33]);
            data.ScatterCount = long.Parse(fields[34]);
            data.ScatterInnerRadius = double.Parse(fields[35]);
            data.ScatterOuterRadius = double.Parse(fields[36]);

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
