using DummyClient.Config;
using DummyClient.Nav;

namespace DummyClient.Sim
{
    // 모든 봇이 공유하는 읽기 전용 컨텍스트 + 공유 시계.
    public sealed class BotContext
    {
        public DummyConfig Config { get; }
        public StageCatalog Catalog { get; }
        public SkillCatalog Skills { get; }
        public string NavMeshDir { get; }

        // 매 틱 매니저가 갱신하는 공유 시각(ms). 봇들이 재접속 예약/타이밍에 사용.
        public long NowMs { get; set; }

        public BotContext(DummyConfig config, StageCatalog catalog, SkillCatalog skills, string navMeshDir)
        {
            Config = config;
            Catalog = catalog;
            Skills = skills;
            NavMeshDir = navMeshDir;
        }
    }
}
