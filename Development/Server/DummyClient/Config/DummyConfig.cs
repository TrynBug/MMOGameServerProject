using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace DummyClient.Config
{
    public sealed class AccountCfg
    {
        public string Prefix { get; set; } = "dummy";
        public int Start { get; set; } = 1;
        public int End { get; set; } = 100;
    }

    public sealed class SpawnCfg
    {
        public int RampPerSec { get; set; } = 10;
    }

    public sealed class MoveCfg
    {
        public int SendIntervalMs { get; set; } = 100;
        public float MoveSpeed { get; set; } = 5.0f;
        public float ArriveDistance { get; set; } = 0.6f;
        public int RepickDelayMs { get; set; } = 500;
    }

    public sealed class TownCfg
    {
        public int RoamCheckIntervalMs { get; set; } = 10000; // 마을 배회 시도 주기
        public double RoamProbability { get; set; } = 0.2;   // 주기마다 실제 배회할 확률
    }

    public sealed class ReconnectCfg
    {
        public bool Enabled { get; set; } = true;
        public int DelayMs { get; set; } = 3000;
    }

    public sealed class CreateCfg
    {
        // 신규 캐릭터의 직업별 선택 확률 (1=Mage, 2=Warrior). 합계는 1이어야 한다.
        public Dictionary<int, double> JobProbabilities { get; set; } = new()
        {
            [1] = 0.5,
            [2] = 0.5,
        };
        // CharacterFactory가 지원하는 외형 프리셋 인덱스(현재 직업당 0, 1).
        public int[] AppearancePresetIds { get; set; } = { 0, 1 };

        public int SelectJobId(double roll)
        {
            double cumulative = 0.0;
            int selectedJobId = 0;
            foreach (var (jobId, probability) in JobProbabilities)
            {
                selectedJobId = jobId;
                cumulative += probability;
                if (roll < cumulative)
                    return jobId;
            }
            return selectedJobId;
        }
    }

    public sealed class SkillCfg
    {
        public Dictionary<int, int> UseIntervalMsByStage { get; set; } = new()
        {
            [100] = 0,    // Town: 스킬 사용 안 함
            [101] = 700,  // Field
            [107] = 2500, // Raid
        };
        public float Range { get; set; } = 12.0f;       // 이 거리 내면 스킬 시전(사거리)
        public float DetectRange { get; set; } = 16.0f; // 이 거리 내 적 발견 시 전투 개시/추격
        public Dictionary<int, int[]> SkillsByJob { get; set; } = new()
        {
            [1] = new[] { 1001, 1003, 1008 }, // Mage: Fireball/IceField/FirePillar
            [2] = new[] { 2001, 2002, 2003 }, // Warrior: NormalAttack/Whirlwind/HeavyStrike
        };

        public int[] GetSkillKeys(int jobId)
            => SkillsByJob != null && SkillsByJob.TryGetValue(jobId, out int[] keys) ? keys : null;

        public int GetUseIntervalMs(int stageKey)
            => UseIntervalMsByStage != null && UseIntervalMsByStage.TryGetValue(stageKey, out int intervalMs) ? intervalMs : 0;
    }

    public sealed class StageMoveCfg
    {
        public int IntervalMs { get; set; } = 45000;   // 스테이지이동 시도 주기
        public double Probability { get; set; } = 0.15; // 주기마다 실제 이동할 확률
        public int PortalTimeoutMs { get; set; } = 15000; // 포탈 탐색+접근 제한시간
        public int ResponseTimeoutMs { get; set; } = 10000; // 상호작용 후 StageMoveRes 제한시간
        public int LoadTimeoutMs { get; set; } = 15000; // StageLoadCompleteRes 제한시간
    }

    public sealed class DisconnectCfg
    {
        public int CheckIntervalMs { get; set; } = 15000; // 연결끊기 판정 주기
        public double Probability { get; set; } = 0.03;   // 주기마다 끊을 확률
    }

    public sealed class DummyConfig
    {
        public string LoginIp { get; set; } = "127.0.0.1";
        public int LoginPort { get; set; } = 8001;

        // 최대 접속 클라이언트 수(상한). 0 이하면 무제한(계정 범위 전체 사용).
        // 실제 봇 수 = min(계정 범위 크기, MaxClients).
        public int MaxClients { get; set; } = 1;

        public AccountCfg Account { get; set; } = new();
        public SpawnCfg Spawn { get; set; } = new();
        public MoveCfg Move { get; set; } = new();
        public TownCfg Town { get; set; } = new();
        public int[] StageKeys { get; set; } = { 100, 101, 107 };
        public CreateCfg Create { get; set; } = new();
        public SkillCfg Skill { get; set; } = new();
        public StageMoveCfg StageMove { get; set; } = new();
        public DisconnectCfg Disconnect { get; set; } = new();
        public string NavMeshDir { get; set; } = "../../../../OUTPUT/Map/NavMesh";
        public string StageDataCsv { get; set; } = "../../../../OUTPUT/GameData/Stage.csv";
        public int TickRateHz { get; set; } = 20;
        public int StatusPrintIntervalMs { get; set; } = 1000;
        public ReconnectCfg Reconnect { get; set; } = new();

        // 계정 범위 크기 (dummyStart .. dummyEnd)
        [JsonIgnore]
        public int PoolSize => System.Math.Max(0, (Account?.End ?? 0) - (Account?.Start ?? 1) + 1);

        // 실제 띄울 봇(클라) 수. MaxClients 상한 적용.
        [JsonIgnore]
        public int BotCount => MaxClients > 0 ? System.Math.Min(PoolSize, MaxClients) : PoolSize;

        // 실행파일 위치 기준으로 상대경로를 절대경로로 해석
        public string ResolvePath(string p)
        {
            if (string.IsNullOrEmpty(p)) return p;
            return Path.IsPathRooted(p) ? p : Path.GetFullPath(p, System.AppContext.BaseDirectory);
        }

        public static DummyConfig Load(string path)
        {
            if (!File.Exists(path))
            {
                System.Console.WriteLine($"[config] '{path}' 없음 → 기본값 사용");
                return new DummyConfig();
            }

            var opt = new JsonSerializerOptions
            {
                PropertyNameCaseInsensitive = true,
                ReadCommentHandling = JsonCommentHandling.Skip,
                AllowTrailingCommas = true,
                NumberHandling = JsonNumberHandling.AllowReadingFromString,
            };
            // 주석 대용 "//" 필드는 무시됨(매핑되는 프로퍼티 없음).
            var cfg = JsonSerializer.Deserialize<DummyConfig>(File.ReadAllText(path), opt);
            return cfg ?? new DummyConfig();
        }

        public bool Validate(out string error)
        {
            if (Account == null || Spawn == null || Move == null || Town == null || Create == null || Skill == null || StageMove == null || Disconnect == null || Reconnect == null)
            {
                error = "설정 섹션 중 null 값이 있음";
                return false;
            }
            if (TickRateHz <= 0 || StatusPrintIntervalMs <= 0 || Move.SendIntervalMs <= 0 || Move.RepickDelayMs <= 0)
            {
                error = "tick/status/move 간격은 0보다 커야 함";
                return false;
            }
            if (Move.MoveSpeed <= 0f || Move.ArriveDistance <= 0f || Skill.Range <= 0f || Skill.DetectRange < Skill.Range)
            {
                error = "이동속도/도착거리/스킬 사거리는 양수이고 detectRange는 range 이상이어야 함";
                return false;
            }
            if (Town.RoamCheckIntervalMs <= 0 || StageMove.IntervalMs <= 0 || StageMove.PortalTimeoutMs <= 0 || StageMove.ResponseTimeoutMs <= 0 || StageMove.LoadTimeoutMs <= 0)
            {
                error = "town/stageMove 시간 설정은 0보다 커야 함";
                return false;
            }
            if (Disconnect.CheckIntervalMs <= 0 || Reconnect.DelayMs < 0 || Account.End < Account.Start)
            {
                error = "disconnect/reconnect/account 범위 설정이 잘못됨";
                return false;
            }
            if (!isProbability(Town.RoamProbability) || !isProbability(StageMove.Probability) || !isProbability(Disconnect.Probability))
            {
                error = "probability 값은 0~1 범위여야 함";
                return false;
            }
            if (StageKeys == null || StageKeys.Length == 0 || Create.JobProbabilities == null || Create.JobProbabilities.Count == 0 || Create.AppearancePresetIds == null || Create.AppearancePresetIds.Length == 0 || Skill.UseIntervalMsByStage == null || Skill.SkillsByJob == null || Skill.SkillsByJob.Count == 0)
            {
                error = "stageKeys, create.jobProbabilities, create.appearancePresetIds, skill.useIntervalMsByStage, skill.skillsByJob은 비어 있을 수 없음";
                return false;
            }
            foreach (int stageKey in StageKeys)
            {
                if (!Skill.UseIntervalMsByStage.TryGetValue(stageKey, out int intervalMs) || intervalMs < 0)
                {
                    error = $"stage별 스킬 시전 간격이 없거나 음수임: stageKey={stageKey}";
                    return false;
                }
            }
            foreach (int presetId in Create.AppearancePresetIds)
            {
                if (presetId < 0)
                {
                    error = $"appearancePresetId는 0 이상이어야 함: presetId={presetId}";
                    return false;
                }
            }
            double jobProbabilitySum = 0.0;
            foreach (var (jobId, probability) in Create.JobProbabilities)
            {
                int[] keys = Skill.GetSkillKeys(jobId);
                if (jobId <= 0 || !isProbability(probability) || keys == null || keys.Length == 0)
                {
                    error = $"create.jobProbabilities의 직업/확률/스킬 설정이 잘못됨: jobId={jobId}, probability={probability}";
                    return false;
                }
                jobProbabilitySum += probability;
            }
            if (System.Math.Abs(jobProbabilitySum - 1.0) > 0.000001)
            {
                error = $"create.jobProbabilities의 확률 합계는 1이어야 함: sum={jobProbabilitySum}";
                return false;
            }

            error = "";
            return true;
        }

        private static bool isProbability(double value) => value >= 0.0 && value <= 1.0;
    }
}
