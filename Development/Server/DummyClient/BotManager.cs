using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;
using DummyClient.Config;
using DummyClient.Nav;
using DummyClient.Sim;

namespace DummyClient
{
    // 봇들을 생성하고, 램프(초당 N개)로 접속시키며, 중앙 틱 루프로 전부 구동한다.
    public sealed class BotManager
    {
        private readonly DummyConfig m_cfg;
        private readonly BotContext m_ctx;
        private readonly List<Bot> m_bots = new();
        private int m_started;

        public IReadOnlyList<Bot> Bots => m_bots;

        public BotManager(DummyConfig cfg, StageCatalog catalog, SkillCatalog skills, string navMeshDir)
        {
            m_cfg = cfg;
            m_ctx = new BotContext(cfg, catalog, skills, navMeshDir);

            for (int i = 0; i < cfg.BotCount; i++)
            {
                int num = cfg.Account.Start + i;
                string id = cfg.Account.Prefix + num;
                m_bots.Add(new Bot(i, id, m_ctx));
            }
        }

        public async Task RunAsync(CancellationToken ct)
        {
            var clock = Stopwatch.StartNew();
            double tickMs = 1000.0 / Math.Max(1, m_cfg.TickRateHz);
            long last = clock.ElapsedMilliseconds;

            while (!ct.IsCancellationRequested)
            {
                long now = clock.ElapsedMilliseconds;
                int dt = (int)(now - last);
                last = now;
                m_ctx.NowMs = now;

                RampSpawn(now);

                for (int i = 0; i < m_started; i++)
                    m_bots[i].Tick(now, dt);

                try { await Task.Delay((int)tickMs, ct); }
                catch (TaskCanceledException) { break; }
            }
        }

        // 초당 RampPerSec 개씩만 새로 활성화(= Tick 대상에 포함).
        private void RampSpawn(long nowMs)
        {
            int allowed = m_cfg.Spawn.RampPerSec <= 0
                ? m_bots.Count
                : (int)(nowMs / 1000.0 * m_cfg.Spawn.RampPerSec) + m_cfg.Spawn.RampPerSec;

            if (allowed > m_bots.Count) allowed = m_bots.Count;
            m_started = allowed; // 앞에서부터 순차 활성화 (Bot 은 Idle→접속 자동 진행)
        }
    }
}
