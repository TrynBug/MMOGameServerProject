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
        public int                  Key                  = 0;
        public string               Name                 = "";
        public string               ProjectilePrefabPath = "";
        public string               EffectPrefabPath     = "";
        public string               CastAnim             = "";
        public EJob                 Job                  = EJob.None;
        public ESkillCastClass      CastClass            = ESkillCastClass.None;
        public bool                 IsPrimaryEligible    = false;
        public int                  OnHitSkillKey        = 0;
        public int                  NextSkillKey         = 0;
        public ENextSkillTiming     NextTriggerTiming    = ENextSkillTiming.None;
        public int                  NextTriggerDelayMs   = 0;
        public ENextSkillOrigin     NextOrigin           = ENextSkillOrigin.None;
        public float                CasterFrontDistance  = 0f;
        public int                  CooldownMs           = 0;
        public float                ManaCost             = 0f;
        public int                  CastDelayMs          = 0;
        public int                  ActionLockMs         = 0;
        public bool                 Rotation             = true;
        public ETargetingMode       Targeting            = ETargetingMode.None;
        public ESkillPlacement      Placement            = ESkillPlacement.None;
        public ESkillEffectMotion   EffectMotion         = ESkillEffectMotion.None;
        public ESkillEffectDamage   EffectDamage         = ESkillEffectDamage.None;
        public ESkillEffectShape    EffectShape          = ESkillEffectShape.None;
        public double               DamageCoeff          = 0;
        public float                Radius               = 0f;
        public float                ObbWidth             = 0f;
        public float                ObbLength            = 0f;
        public float                ProjectileSpeed      = 0f;
        public float                MaxRange             = 0f;
        public int                  ProjectileCount      = 0;
        public float                FanAngleDeg          = 0f;
        public int                  FirstTickDelayMs     = 0;
        public int                  TickIntervalMs       = 0;
        public int                  LifetimeMs           = 0;
        public float                MoveDistance         = 0f;
        public int                  MoveDurationMs       = 0;
        public int                  ScatterCount         = 0;
        public float                ScatterInnerRadius   = 0f;
        public float                ScatterOuterRadius   = 0f;
    }

    // Skill 데이터 파일 전체를 표현합니다.
    public class GameDataTableBase_Skill : GameDataTableBase
    {
        protected static Dictionary<int, GameData_Skill> sm_dataMap = new();

        public override string GetDataName() => "Skill";

        public static GameData_Skill FindData(int key)
        {
            sm_dataMap.TryGetValue(key, out var data);
            return data;
        }

        public static IReadOnlyDictionary<int, GameData_Skill> GetDataMap() => sm_dataMap;

        protected override bool MakeGameData(string line)
        {
            string[] fields = line.Split(',');
            GameData_Skill data = new GameData_Skill();

            data.Key = int.Parse(fields[0]);
            data.Name = fields[1];
            data.ProjectilePrefabPath = fields[2];
            data.EffectPrefabPath = fields[3];
            data.CastAnim = fields[4];
            data.Job = (EJob)int.Parse(fields[5]);
            data.CastClass = (ESkillCastClass)int.Parse(fields[6]);
            data.IsPrimaryEligible = GameDataTableBase.StringToBool(fields[7]);
            data.OnHitSkillKey = int.Parse(fields[8]);
            data.NextSkillKey = int.Parse(fields[9]);
            data.NextTriggerTiming = (ENextSkillTiming)int.Parse(fields[10]);
            data.NextTriggerDelayMs = int.Parse(fields[11]);
            data.NextOrigin = (ENextSkillOrigin)int.Parse(fields[12]);
            data.CasterFrontDistance = float.Parse(fields[13]);
            data.CooldownMs = int.Parse(fields[14]);
            data.ManaCost = float.Parse(fields[15]);
            data.CastDelayMs = int.Parse(fields[16]);
            data.ActionLockMs = int.Parse(fields[17]);
            data.Rotation = GameDataTableBase.StringToBool(fields[18]);
            data.Targeting = (ETargetingMode)int.Parse(fields[19]);
            data.Placement = (ESkillPlacement)int.Parse(fields[20]);
            data.EffectMotion = (ESkillEffectMotion)int.Parse(fields[21]);
            data.EffectDamage = (ESkillEffectDamage)int.Parse(fields[22]);
            data.EffectShape = (ESkillEffectShape)int.Parse(fields[23]);
            data.DamageCoeff = double.Parse(fields[24]);
            data.Radius = float.Parse(fields[25]);
            data.ObbWidth = float.Parse(fields[26]);
            data.ObbLength = float.Parse(fields[27]);
            data.ProjectileSpeed = float.Parse(fields[28]);
            data.MaxRange = float.Parse(fields[29]);
            data.ProjectileCount = int.Parse(fields[30]);
            data.FanAngleDeg = float.Parse(fields[31]);
            data.FirstTickDelayMs = int.Parse(fields[32]);
            data.TickIntervalMs = int.Parse(fields[33]);
            data.LifetimeMs = int.Parse(fields[34]);
            data.MoveDistance = float.Parse(fields[35]);
            data.MoveDurationMs = int.Parse(fields[36]);
            data.ScatterCount = int.Parse(fields[37]);
            data.ScatterInnerRadius = float.Parse(fields[38]);
            data.ScatterOuterRadius = float.Parse(fields[39]);

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
