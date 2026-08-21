using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;
using System.Threading;

namespace DummyClient.Metrics
{
    public enum LatencyKind
    {
        LoginConnect,
        Login,
        GatewayConnect,
        GatewayAuth,
        CharacterCreate,
        CharacterSelect,
        StageLoad,
        StageMove,
        GatewayPing,
        GamePing,
    }

    public readonly record struct CounterSnapshot(long Packets, long Bytes);
    public readonly record struct LatencySnapshot(int Count, double AverageMs, double P95Ms, double MaxMs);
    public readonly record struct ErrorSnapshot(string Reason, long Count);

    public sealed class DummyMetrics
    {
        private sealed class PacketCounter
        {
            public long Packets;
            public long Bytes;
        }

        private sealed class LatencyWindow
        {
            private const int Capacity = 4096;
            private readonly Queue<double> m_samples = new(Capacity);
            private double m_sum;

            public void Record(double elapsedMs)
            {
                lock (m_samples)
                {
                    if (m_samples.Count == Capacity)
                        m_sum -= m_samples.Dequeue();
                    m_samples.Enqueue(elapsedMs);
                    m_sum += elapsedMs;
                }
            }

            public LatencySnapshot Snapshot()
            {
                lock (m_samples)
                {
                    if (m_samples.Count == 0) return default;
                    double[] sorted = m_samples.ToArray();
                    Array.Sort(sorted);
                    int p95Index = Math.Min(sorted.Length - 1, (int)Math.Ceiling(sorted.Length * 0.95) - 1);
                    return new LatencySnapshot(sorted.Length, (double)m_sum / sorted.Length, sorted[p95Index], sorted[^1]);
                }
            }
        }

        private readonly ConcurrentDictionary<ushort, PacketCounter> m_sentByType = new();
        private readonly ConcurrentDictionary<ushort, PacketCounter> m_recvByType = new();
        private readonly ConcurrentDictionary<string, long> m_errorTotals = new(StringComparer.Ordinal);
        private readonly Queue<(long TimeMs, string Reason)> m_recentErrors = new();
        private readonly LatencyWindow[] m_latencies;

        private long m_packetsSent;
        private long m_packetsRecv;
        private long m_bytesSent;
        private long m_bytesRecv;
        private long m_tickCount;
        private long m_tickProcessingUs;
        private long m_tickOverruns;
        private long m_tickMaxUsSincePrint;
        private long m_recvQueueDepth;
        private long m_recvQueueMaxSincePrint;

        public DummyMetrics()
        {
            m_latencies = new LatencyWindow[Enum.GetValues<LatencyKind>().Length];
            for (int i = 0; i < m_latencies.Length; i++)
                m_latencies[i] = new LatencyWindow();
        }

        public CounterSnapshot Sent => new(Interlocked.Read(ref m_packetsSent), Interlocked.Read(ref m_bytesSent));
        public CounterSnapshot Recv => new(Interlocked.Read(ref m_packetsRecv), Interlocked.Read(ref m_bytesRecv));
        public long TickCount => Interlocked.Read(ref m_tickCount);
        public long TickProcessingUs => Interlocked.Read(ref m_tickProcessingUs);
        public long TickOverruns => Interlocked.Read(ref m_tickOverruns);
        public long RecvQueueDepth => Interlocked.Read(ref m_recvQueueDepth);
        public long ErrorCount => m_errorTotals.Sum(kv => kv.Value);

        public void RecordSent(ushort type, int bytes)
        {
            Interlocked.Increment(ref m_packetsSent);
            Interlocked.Add(ref m_bytesSent, bytes);
            RecordPacket(m_sentByType, type, bytes);
        }

        public void RecordRecv(ushort type, int bytes)
        {
            Interlocked.Increment(ref m_packetsRecv);
            Interlocked.Add(ref m_bytesRecv, bytes);
            RecordPacket(m_recvByType, type, bytes);
        }

        public IReadOnlyDictionary<ushort, CounterSnapshot> SnapshotSentByType() => SnapshotPackets(m_sentByType);
        public IReadOnlyDictionary<ushort, CounterSnapshot> SnapshotRecvByType() => SnapshotPackets(m_recvByType);

        public void RecordLatency(LatencyKind kind, double elapsedMs)
        {
            if (elapsedMs >= 0)
                m_latencies[(int)kind].Record(elapsedMs);
        }

        public LatencySnapshot GetLatency(LatencyKind kind) => m_latencies[(int)kind].Snapshot();

        public void RecordError(string reason, long nowMs)
        {
            if (string.IsNullOrWhiteSpace(reason)) reason = "unknown";
            m_errorTotals.AddOrUpdate(reason, 1, (_, count) => count + 1);
            lock (m_recentErrors)
            {
                m_recentErrors.Enqueue((nowMs, reason));
                PruneErrors(nowMs - 60000);
            }
        }

        public IReadOnlyList<ErrorSnapshot> GetRecentErrors(long nowMs, int maxCount)
        {
            lock (m_recentErrors)
            {
                PruneErrors(nowMs - 60000);
                return m_recentErrors
                    .GroupBy(e => e.Reason, StringComparer.Ordinal)
                    .Select(g => new ErrorSnapshot(g.Key, g.LongCount()))
                    .OrderByDescending(e => e.Count)
                    .ThenBy(e => e.Reason, StringComparer.Ordinal)
                    .Take(maxCount)
                    .ToArray();
            }
        }

        public long RecentErrorCount(long nowMs)
        {
            lock (m_recentErrors)
            {
                PruneErrors(nowMs - 60000);
                return m_recentErrors.Count;
            }
        }

        public void RecordTick(double processingMs, double targetTickMs)
        {
            long us = Math.Max(0, (long)Math.Round(processingMs * 1000.0));
            Interlocked.Increment(ref m_tickCount);
            Interlocked.Add(ref m_tickProcessingUs, us);
            if (processingMs > targetTickMs)
                Interlocked.Increment(ref m_tickOverruns);
            AtomicMax(ref m_tickMaxUsSincePrint, us);
        }

        public void RecordRecvEnqueued()
        {
            long depth = Interlocked.Increment(ref m_recvQueueDepth);
            AtomicMax(ref m_recvQueueMaxSincePrint, depth);
        }

        public void RecordRecvDequeued() => Interlocked.Decrement(ref m_recvQueueDepth);
        public long ConsumeTickMaxUs() => Interlocked.Exchange(ref m_tickMaxUsSincePrint, 0);
        public long ConsumeRecvQueueMax() => Interlocked.Exchange(ref m_recvQueueMaxSincePrint, 0);

        private static void RecordPacket(ConcurrentDictionary<ushort, PacketCounter> counters, ushort type, int bytes)
        {
            PacketCounter counter = counters.GetOrAdd(type, _ => new PacketCounter());
            Interlocked.Increment(ref counter.Packets);
            Interlocked.Add(ref counter.Bytes, bytes);
        }

        private static IReadOnlyDictionary<ushort, CounterSnapshot> SnapshotPackets(ConcurrentDictionary<ushort, PacketCounter> counters)
        {
            var snapshot = new Dictionary<ushort, CounterSnapshot>(counters.Count);
            foreach (var (type, counter) in counters)
                snapshot[type] = new CounterSnapshot(Interlocked.Read(ref counter.Packets), Interlocked.Read(ref counter.Bytes));
            return snapshot;
        }

        private void PruneErrors(long cutoffMs)
        {
            while (m_recentErrors.Count > 0 && m_recentErrors.Peek().TimeMs < cutoffMs)
                m_recentErrors.Dequeue();
        }

        private static void AtomicMax(ref long target, long value)
        {
            long current = Interlocked.Read(ref target);
            while (value > current)
            {
                long observed = Interlocked.CompareExchange(ref target, value, current);
                if (observed == current) return;
                current = observed;
            }
        }
    }
}
