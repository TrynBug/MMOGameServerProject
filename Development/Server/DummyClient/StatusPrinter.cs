using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Text;
using Common;
using DummyClient.Metrics;
using DummyClient.Sim;

namespace DummyClient
{
    public static class StatusPrinter
    {
        private static long s_lastElapsedMs;
        private static CounterSnapshot s_lastSent;
        private static CounterSnapshot s_lastRecv;
        private static long s_lastTickCount;
        private static long s_lastTickProcessingUs;
        private static long s_lastTickOverruns;
        private static long s_lastAllocatedBytes;
        private static TimeSpan s_lastCpuTime;
        private static double s_peakSentPps;
        private static double s_peakRecvPps;
        private static double s_peakSentBytesPerSec;
        private static double s_peakRecvBytesPerSec;
        private static IReadOnlyDictionary<ushort, CounterSnapshot> s_lastSentByType = new Dictionary<ushort, CounterSnapshot>();
        private static IReadOnlyDictionary<ushort, CounterSnapshot> s_lastRecvByType = new Dictionary<ushort, CounterSnapshot>();

        private static int s_lastSkillRequests;
        private static int s_lastSkillsAccepted;
        private static int s_lastProjectileHits;

        public static void Print(BotManager manager, long elapsedMs)
        {
            IReadOnlyList<Bot> bots = manager.Bots;
            DummyMetrics metrics = manager.Metrics;
            int started = Math.Min(manager.StartedCount, bots.Count);
            double intervalSec = Math.Max(0.001, (elapsedMs - s_lastElapsedMs) / 1000.0);

            var counts = new int[Enum.GetValues<BotState>().Length];
            int connected = 0, loggedIn = 0, fighting = 0, dead = 0;
            int skillRequests = 0, skillsAccepted = 0, projectileHits = 0;
            int stageMoves = 0, deaths = 0, revives = 0, reconnects = 0, stageLoadTimeouts = 0;
            int portalAttempts = 0, portalFailures = 0, portalTimeouts = 0;
            int returnAttempts = 0, returns = 0, returnFailures = 0;
            var stageDist = new SortedDictionary<int, int>();
            var jobDist = new SortedDictionary<int, int>();

            for (int i = 0; i < started; i++)
            {
                Bot bot = bots[i];
                counts[(int)bot.State]++;
                if (bot.IsConnected) connected++;
                if (bot.IsLoggedIn) loggedIn++;
                if (bot.IsFighting) fighting++;
                if (bot.IsDead) dead++;
                skillRequests += bot.SkillCastRequests;
                skillsAccepted += bot.SkillsCast;
                projectileHits += bot.ProjectileHitsSent;
                stageMoves += bot.StageMoves;
                deaths += bot.Deaths;
                revives += bot.Revives;
                reconnects += bot.Reconnects;
                stageLoadTimeouts += bot.StageLoadTimeouts;
                portalAttempts += bot.PortalAttempts;
                portalFailures += bot.PortalFailures;
                portalTimeouts += bot.PortalTimeouts;
                returnAttempts += bot.DirectReturnAttempts;
                returns += bot.DirectReturns;
                returnFailures += bot.DirectReturnFailures;
                if (bot.JobId > 0)
                    jobDist[bot.JobId] = jobDist.GetValueOrDefault(bot.JobId) + 1;
                if (bot.State == BotState.InStage)
                    stageDist[bot.StageDataKey] = stageDist.GetValueOrDefault(bot.StageDataKey) + 1;
            }

            CounterSnapshot sent = metrics.Sent;
            CounterSnapshot recv = metrics.Recv;
            double sentPps = (sent.Packets - s_lastSent.Packets) / intervalSec;
            double recvPps = (recv.Packets - s_lastRecv.Packets) / intervalSec;
            double sentBytesPerSec = (sent.Bytes - s_lastSent.Bytes) / intervalSec;
            double recvBytesPerSec = (recv.Bytes - s_lastRecv.Bytes) / intervalSec;
            s_peakSentPps = Math.Max(s_peakSentPps, sentPps);
            s_peakRecvPps = Math.Max(s_peakRecvPps, recvPps);
            s_peakSentBytesPerSec = Math.Max(s_peakSentBytesPerSec, sentBytesPerSec);
            s_peakRecvBytesPerSec = Math.Max(s_peakRecvBytesPerSec, recvBytesPerSec);

            long tickCount = metrics.TickCount;
            long tickProcessingUs = metrics.TickProcessingUs;
            long tickOverruns = metrics.TickOverruns;
            long intervalTicks = tickCount - s_lastTickCount;
            double actualTickHz = intervalTicks / intervalSec;
            double averageTickMs = intervalTicks > 0 ? (tickProcessingUs - s_lastTickProcessingUs) / 1000.0 / intervalTicks : 0;
            double maxTickMs = metrics.ConsumeTickMaxUs() / 1000.0;
            long intervalOverruns = tickOverruns - s_lastTickOverruns;
            long maxRecvQueue = metrics.ConsumeRecvQueueMax();

            IReadOnlyDictionary<ushort, CounterSnapshot> sentByType = metrics.SnapshotSentByType();
            IReadOnlyDictionary<ushort, CounterSnapshot> recvByType = metrics.SnapshotRecvByType();
            string topSent = FormatTopPackets(sentByType, s_lastSentByType, intervalSec);
            string topRecv = FormatTopPackets(recvByType, s_lastRecvByType, intervalSec);

            Process process = Process.GetCurrentProcess();
            process.Refresh();
            TimeSpan cpuTime = process.TotalProcessorTime;
            double cpuPercent = s_lastElapsedMs > 0
                ? (cpuTime - s_lastCpuTime).TotalMilliseconds / (intervalSec * 1000.0 * Environment.ProcessorCount) * 100.0
                : 0;
            long allocatedBytes = GC.GetTotalAllocatedBytes(false);
            double allocatedPerSec = s_lastElapsedMs > 0 ? (allocatedBytes - s_lastAllocatedBytes) / intervalSec : 0;
            int threadCount;
            try { threadCount = process.Threads.Count; }
            catch { threadCount = 0; }

            int ready = counts[(int)BotState.InStage];
            double readyPercent = started > 0 ? ready * 100.0 / started : 0;
            double skillReqRate = (skillRequests - s_lastSkillRequests) / intervalSec;
            double skillAcceptRate = (skillsAccepted - s_lastSkillsAccepted) / intervalSec;
            double projectileRate = (projectileHits - s_lastProjectileHits) / intervalSec;

            var sb = new StringBuilder(4096);
            sb.AppendLine("────────────────────────────────────────────────────────────────────────────────");
            sb.AppendLine($" DummyClient | uptime {FormatDuration(elapsedMs)} | target {bots.Count} | started {started} | ready {ready} ({readyPercent:F1}%)");
            sb.AppendLine("────────────────────────────────────────────────────────────────────────────────");
            sb.AppendLine($" Bots    connected {connected} | loggedIn {loggedIn} | fighting {fighting} | roaming {Math.Max(0, ready - fighting - dead)} | dead {dead}");
            sb.AppendLine($" States  idle {counts[(int)BotState.Idle]} | login {counts[(int)BotState.ConnectingLogin] + counts[(int)BotState.WaitLoginRes]} | gateway {counts[(int)BotState.NeedGatewayConnect] + counts[(int)BotState.ConnectingGateway] + counts[(int)BotState.WaitCharList]} | create {counts[(int)BotState.WaitCreate]} | select {counts[(int)BotState.WaitSelect]} | load {counts[(int)BotState.WaitStageLoad]} | moving {counts[(int)BotState.WaitStageMoveRes]} | reconnect {counts[(int)BotState.Disconnected]}");
            if (stageDist.Count > 0) sb.AppendLine($" Stages  {FormatDistribution(stageDist)}");
            if (jobDist.Count > 0) sb.AppendLine($" Jobs    {FormatDistribution(jobDist)}");
            sb.AppendLine();
            sb.AppendLine($" OUT app {sent.Packets:N0} pkts | total {FormatBytes(sent.Bytes)} | {sentPps:N1} pkt/s | {FormatBytes(sentBytesPerSec)}/s | avg {AverageBytes(sent):N1} B | peak {s_peakSentPps:N1} pkt/s {FormatBytes(s_peakSentBytesPerSec)}/s");
            sb.AppendLine($" IN  app {recv.Packets:N0} pkts | total {FormatBytes(recv.Bytes)} | {recvPps:N1} pkt/s | {FormatBytes(recvBytesPerSec)}/s | avg {AverageBytes(recv):N1} B | peak {s_peakRecvPps:N1} pkt/s {FormatBytes(s_peakRecvBytesPerSec)}/s");
            if (topSent.Length > 0) sb.AppendLine($" OUT top {topSent}");
            if (topRecv.Length > 0) sb.AppendLine($" IN  top {topRecv}");
            sb.AppendLine();
            sb.AppendLine($" Tick    target {manager.TargetTickRateHz}Hz | actual {actualTickHz:F1}Hz | avg {averageTickMs:F2}ms | max {maxTickMs:F2}ms | overruns +{intervalOverruns} ({tickOverruns:N0})");
            sb.AppendLine($" Queue   recv pending {Math.Max(0, metrics.RecvQueueDepth):N0} | interval max {maxRecvQueue:N0}");
            sb.AppendLine($" Client  CPU {cpuPercent:F1}% | working {FormatBytes(process.WorkingSet64)} | private {FormatBytes(process.PrivateMemorySize64)} | GC {FormatBytes(GC.GetTotalMemory(false))} | alloc {FormatBytes(allocatedPerSec)}/s | threads {threadCount}");
            AppendLatencies(sb, metrics);
            sb.AppendLine();
            sb.AppendLine($" Skills  req {skillRequests:N0} ({skillReqRate:N1}/s) | accepted {skillsAccepted:N0} ({skillAcceptRate:N1}/s) | hits {projectileHits:N0} ({projectileRate:N1}/s)");
            sb.AppendLine($" Moves   portal {stageMoves - returns}/{portalAttempts} fail {portalFailures} timeout {portalTimeouts} | return {returns}/{returnAttempts} fail {returnFailures} | loadTimeout {stageLoadTimeouts}");
            sb.AppendLine($" Life    deaths {deaths} | revives {revives} | reconnects {reconnects}");

            IReadOnlyList<ErrorSnapshot> recentErrors = metrics.GetRecentErrors(elapsedMs, 3);
            sb.AppendLine($" Errors  total {metrics.ErrorCount:N0} | last60s {metrics.RecentErrorCount(elapsedMs):N0}");
            foreach (ErrorSnapshot error in recentErrors)
                sb.AppendLine($"         {error.Count,5}  {error.Reason}");

            try { Console.Clear(); }
            catch { }
            Console.Write(sb.ToString());

            s_lastElapsedMs = elapsedMs;
            s_lastSent = sent;
            s_lastRecv = recv;
            s_lastTickCount = tickCount;
            s_lastTickProcessingUs = tickProcessingUs;
            s_lastTickOverruns = tickOverruns;
            s_lastCpuTime = cpuTime;
            s_lastAllocatedBytes = allocatedBytes;
            s_lastSentByType = sentByType;
            s_lastRecvByType = recvByType;
            s_lastSkillRequests = skillRequests;
            s_lastSkillsAccepted = skillsAccepted;
            s_lastProjectileHits = projectileHits;
        }

        private static void AppendLatencies(StringBuilder sb, DummyMetrics metrics)
        {
            var values = new List<string>();
            foreach (LatencyKind kind in Enum.GetValues<LatencyKind>())
            {
                LatencySnapshot latency = metrics.GetLatency(kind);
                if (latency.Count == 0) continue;
                values.Add($"{kind} avg {latency.AverageMs:F0} p95 {latency.P95Ms} max {latency.MaxMs}ms");
            }
            if (values.Count == 0) return;
            sb.AppendLine($" Latency {string.Join(" | ", values.Take(4))}");
            if (values.Count > 4)
                sb.AppendLine($"         {string.Join(" | ", values.Skip(4))}");
        }

        private static string FormatTopPackets(IReadOnlyDictionary<ushort, CounterSnapshot> current,
            IReadOnlyDictionary<ushort, CounterSnapshot> previous, double intervalSec)
        {
            return string.Join(" | ", current
                .Select(kv =>
                {
                    previous.TryGetValue(kv.Key, out CounterSnapshot old);
                    return (Type: kv.Key, Rate: (kv.Value.Packets - old.Packets) / intervalSec);
                })
                .Where(v => v.Rate > 0)
                .OrderByDescending(v => v.Rate)
                .Take(3)
                .Select(v => $"{PacketName(v.Type)} {v.Rate:N1}/s"));
        }

        private static string PacketName(ushort type)
            => Enum.IsDefined(typeof(GamePacketId), (int)type) ? ((GamePacketId)type).ToString() : type.ToString();

        private static string FormatDistribution(IEnumerable<KeyValuePair<int, int>> values)
            => string.Join(" | ", values.Select(kv => $"[{kv.Key}] {kv.Value}"));

        private static double AverageBytes(CounterSnapshot snapshot)
            => snapshot.Packets > 0 ? (double)snapshot.Bytes / snapshot.Packets : 0;

        private static string FormatBytes(double bytes)
        {
            string[] units = { "B", "KiB", "MiB", "GiB" };
            int unit = 0;
            double value = Math.Max(0, bytes);
            while (value >= 1024.0 && unit < units.Length - 1)
            {
                value /= 1024.0;
                unit++;
            }
            return $"{value:F1} {units[unit]}";
        }

        private static string FormatDuration(long elapsedMs)
        {
            TimeSpan elapsed = TimeSpan.FromMilliseconds(elapsedMs);
            return elapsed.Days > 0
                ? $"{elapsed.Days}.{elapsed:hh\\:mm\\:ss}"
                : elapsed.ToString(@"hh\:mm\:ss");
        }
    }
}
