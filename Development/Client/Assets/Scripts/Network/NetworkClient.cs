using System;
using System.Collections.Concurrent;
using System.Net.Sockets;
using System.Threading;

namespace Client.Network
{
    // 순수 C# TCP 클라이언트 (Unity 비의존).
    // - 수신 스레드 1개에서 무한루프로 패킷을 받아 RecvQueue 에 쌓는다.
    // - Send 는 호출 스레드에서 동기로 NetworkStream.Write 한다.
    // - OnConnected/OnDisconnected 이벤트는 임의 스레드에서 호출될 수 있으므로
    //   사용자(NetworkManager)는 반드시 메인 스레드로 다시 디스패치해야 한다.
    public class NetworkClient
    {
        private TcpClient m_tcpClient;
        private NetworkStream m_stream;
        private Thread m_recvThread;
        private volatile bool m_running;

        // 수신 스레드 -> 메인 스레드로 전달되는 큐
        public ConcurrentQueue<RawPacket> RecvQueue { get; } = new ConcurrentQueue<RawPacket>();

        public bool IsConnected => m_tcpClient != null && m_tcpClient.Connected;

        // 임의 스레드에서 호출될 수 있음
        public event Action OnConnected;
        public event Action<string> OnDisconnected; // 사유 메시지

        // 동기 connect. 성공/실패를 bool로 반환. UI는 비동기로 감싸서 사용.
        public bool Connect(string ip, int port)
        {
            try
            {
                m_tcpClient = new TcpClient();
                m_tcpClient.NoDelay = true; // Nagle off
                m_tcpClient.Connect(ip, port);
                m_stream = m_tcpClient.GetStream();

                m_running = true;
                m_recvThread = new Thread(recvLoop) { IsBackground = true, Name = "NetRecv" };
                m_recvThread.Start();

                OnConnected?.Invoke();
                return true;
            }
            catch (Exception e)
            {
                cleanup();
                OnDisconnected?.Invoke($"Connect failed: {e.Message}");
                return false;
            }
        }

        public void Disconnect()
        {
            if (!m_running && m_tcpClient == null) return;

            m_running = false;
            cleanup();
            OnDisconnected?.Invoke(null); // 정상 종료
        }

        public void Send(ushort packetType, byte[] body)
        {
            if (!m_running || m_stream == null)
            {
                return;
            }

            byte[] packet = PacketHeaderHelper.MakePacket(packetType, body);
            try
            {
                m_stream.Write(packet, 0, packet.Length);
            }
            catch (Exception e)
            {
                m_running = false;
                cleanup();
                OnDisconnected?.Invoke($"Send failed: {e.Message}");
            }
        }

        private void recvLoop()
        {
            byte[] headerBuf = new byte[PacketHeaderHelper.HeaderSize];

            try
            {
                while (m_running)
                {
                    if (!readExact(headerBuf, PacketHeaderHelper.HeaderSize)) break;

                    PacketHeaderHelper.Parse(headerBuf, out ushort size, out ushort type, out byte flags);

                    if (size < PacketHeaderHelper.HeaderSize)
                    {
                        throw new Exception($"Invalid packet size: {size}");
                    }

                    int bodySize = size - PacketHeaderHelper.HeaderSize;
                    byte[] body = bodySize > 0 ? new byte[bodySize] : Array.Empty<byte>();
                    if (bodySize > 0 && !readExact(body, bodySize)) break;

                    RecvQueue.Enqueue(new RawPacket { Type = type, Flags = flags, Body = body });
                }
            }
            catch (Exception e)
            {
                if (m_running)
                {
                    m_running = false;
                    cleanup();
                    OnDisconnected?.Invoke($"Recv error: {e.Message}");
                }
            }
        }

        // count 바이트를 정확히 읽을 때까지 반복. 실패 시 false.
        private bool readExact(byte[] buf, int count)
        {
            int offset = 0;
            while (offset < count)
            {
                int read;
                try
                {
                    read = m_stream.Read(buf, offset, count - offset);
                }
                catch
                {
                    return false;
                }
                if (read <= 0) return false; // 상대 종료
                offset += read;
            }
            return true;
        }

        private void cleanup()
        {
            try { m_stream?.Close(); } catch { }
            try { m_tcpClient?.Close(); } catch { }
            m_stream = null;
            m_tcpClient = null;
        }
    }
}
