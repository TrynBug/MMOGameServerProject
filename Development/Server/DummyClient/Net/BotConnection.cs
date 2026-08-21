using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using Client.Network;      // PacketHeaderHelper, RawPacket (클라와 공유하는 순수 C#)
using DummyClient.Metrics;
using Google.Protobuf;

namespace DummyClient.Net
{
    // 봇 1개당 TCP 연결 1개. 클라의 NetworkClient(연결당 스레드 1개) 대신
    // NetworkStream.ReadAsync 기반 비동기 수신을 써서 수백~수천 봇으로 확장 가능.
    // 수신 패킷은 RecvQueue 에 쌓이고, 봇의 틱 스레드가 드레인/디스패치한다.
    public sealed class BotConnection
    {
        private TcpClient m_tcp;
        private NetworkStream m_stream;
        private CancellationTokenSource m_cts;
        private readonly SemaphoreSlim m_sendLock = new(1, 1);
        private readonly DummyMetrics m_metrics;
        private int m_closed;

        private readonly record struct ReceivedPacket(RawPacket Packet, long ReceivedTimestamp);
        private readonly ConcurrentQueue<ReceivedPacket> m_recvQueue = new();
        public bool IsConnected => m_tcp is { Connected: true } && m_closed == 0;

        // 임의 스레드에서 호출될 수 있음. reason==null 이면 정상 종료.
        public event Action<string> OnClosed;

        public BotConnection(DummyMetrics metrics)
        {
            m_metrics = metrics;
        }

        public async Task<bool> ConnectAsync(string ip, int port)
        {
            try
            {
                m_tcp = new TcpClient { NoDelay = true };
                await m_tcp.ConnectAsync(ip, port).ConfigureAwait(false);
                m_stream = m_tcp.GetStream();
                m_cts = new CancellationTokenSource();
                _ = Task.Run(() => RecvLoopAsync(m_cts.Token));
                return true;
            }
            catch (Exception e)
            {
                Close($"connect failed: {e.Message}");
                return false;
            }
        }

        public void Send(Common.GamePacketId id, IMessage msg)
        {
            // fire-and-forget. 봇 로직은 단일 틱 스레드에서 낮은 빈도로 보내므로 순서 문제 없음.
            _ = SendAsync(id, msg);
        }

        public async Task SendAsync(Common.GamePacketId id, IMessage msg)
        {
            if (m_stream == null || m_closed != 0) return;

            byte[] body = msg.ToByteArray();
            byte[] packet = PacketHeaderHelper.MakePacket((ushort)id, body);

            await m_sendLock.WaitAsync().ConfigureAwait(false);
            try
            {
                await m_stream.WriteAsync(packet).ConfigureAwait(false);
                m_metrics.RecordSent((ushort)id, packet.Length);
            }
            catch (Exception e)
            {
                Close($"send failed: {e.Message}");
            }
            finally
            {
                m_sendLock.Release();
            }
        }

        private async Task RecvLoopAsync(CancellationToken ct)
        {
            var header = new byte[PacketHeaderHelper.HeaderSize];
            try
            {
                while (!ct.IsCancellationRequested)
                {
                    if (!await ReadFullAsync(header, header.Length, ct).ConfigureAwait(false))
                        break;

                    PacketHeaderHelper.Parse(header, out ushort size, out ushort type, out byte flags);
                    int bodyLen = size - PacketHeaderHelper.HeaderSize;
                    if (bodyLen < 0) { Close("bad packet size"); return; }

                    byte[] body = bodyLen > 0 ? new byte[bodyLen] : Array.Empty<byte>();
                    if (bodyLen > 0 && !await ReadFullAsync(body, bodyLen, ct).ConfigureAwait(false))
                        break;

                    if (m_closed != 0) break;
                    m_recvQueue.Enqueue(new ReceivedPacket(
                        new RawPacket { Type = type, Flags = flags, Body = body },
                        Stopwatch.GetTimestamp()));
                    m_metrics.RecordRecv(type, size);
                    m_metrics.RecordRecvEnqueued();
                    if (m_closed != 0)
                    {
                        while (TryDequeue(out _, out _)) { }
                        break;
                    }
                }
                Close(null); // 정상 종료 (서버가 끊음 또는 취소)
            }
            catch (Exception e)
            {
                Close($"recv failed: {e.Message}");
            }
        }

        private async Task<bool> ReadFullAsync(byte[] buf, int len, CancellationToken ct)
        {
            int read = 0;
            while (read < len)
            {
                int n = await m_stream.ReadAsync(buf.AsMemory(read, len - read), ct).ConfigureAwait(false);
                if (n <= 0) return false; // 원격 종료
                read += n;
            }
            return true;
        }

        public void Close(string reason)
        {
            if (Interlocked.Exchange(ref m_closed, 1) == 1) return;
            try { m_cts?.Cancel(); } catch { }
            try { m_stream?.Close(); } catch { }
            try { m_tcp?.Close(); } catch { }
            while (TryDequeue(out _, out _)) { }
            try { OnClosed?.Invoke(reason); } catch { }
        }

        public bool TryDequeue(out RawPacket packet, out long receivedTimestamp)
        {
            if (!m_recvQueue.TryDequeue(out ReceivedPacket received))
            {
                packet = null;
                receivedTimestamp = 0;
                return false;
            }
            packet = received.Packet;
            receivedTimestamp = received.ReceivedTimestamp;
            m_metrics.RecordRecvDequeued();
            return true;
        }
    }
}
