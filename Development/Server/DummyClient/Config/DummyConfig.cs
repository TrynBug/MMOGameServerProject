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

    public sealed class ReconnectCfg
    {
        public bool Enabled { get; set; } = true;
        public int DelayMs { get; set; } = 3000;
    }

    public sealed class CreateCfg
    {
        // 캐릭터 생성 시 무작위로 고를 직업 목록 (1=Mage, 2=Warrior).
        // 현재 스킬 게임데이터는 Mage(1)만 존재하므로 기본 [1].
        public int[] JobIds { get; set; } = { 1 };
    }

    public sealed class SkillCfg
    {
        public int UseIntervalMs { get; set; } = 700;   // 연속 스킬 시전 간격(스킬 로테이션)
        public float Range { get; set; } = 12.0f;       // 이 거리 내면 스킬 시전(사거리)
        public float DetectRange { get; set; } = 16.0f; // 이 거리 내 적 발견 시 전투 개시/추격
        public int[] SkillKeys { get; set; } = { 1001, 1006, 1012 }; // Mage: Fireball/Blaze/ElectricShock
    }

    public sealed class StageMoveCfg
    {
        public int IntervalMs { get; set; } = 45000;   // 스테이지이동 시도 주기
        public double Probability { get; set; } = 0.6; // 주기마다 실제 이동할 확률
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
        public int[] StageKeys { get; set; } = { 100 };
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
    }
}
