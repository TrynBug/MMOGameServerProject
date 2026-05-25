using System;
using System.Collections.Concurrent;
using Google.Protobuf;
using UnityEngine;

namespace Client.Network
{
    // MonoBehaviour 싱글톤. 씬 전환에도 살아남는다 (DontDestroyOnLoad).
    //
    // 책임:
    //   1) NetworkClient 소유 및 라이프사이클 관리
    //   2) 수신 스레드의 RecvQueue 를 Update() 에서 비우고 OnPacketReceived 이벤트로 전달
    //   3) NetworkClient 의 OnConnected/OnDisconnected 이벤트를 메인 스레드 큐로 마샬링하여
    //      Unity 객체를 안전하게 만질 수 있게 한다.
    //
    // NetworkManager 는 Packet 어셈블리에 의존하지 않는다 (의존 방향: Game → Packet → Network).
    // PacketDispatcher 는 외부(Game)에서 OnPacketReceived 에 구독하여 디스패치를 수행한다.
    public class NetworkManager : MonoBehaviour
    {
        public static NetworkManager Instance { get; private set; }

        private NetworkClient m_client;

        // 메인 스레드에서 호출되는 이벤트 (UI/게임로직에서 안전하게 구독)
        public event Action OnConnected;
        public event Action<string> OnDisconnected; // null = 정상 종료
        public event Action<RawPacket> OnPacketReceived; // 메인 스레드에서 호출됨

        // 임의 스레드에서 오는 이벤트를 메인 스레드로 옮기기 위한 큐
        private readonly ConcurrentQueue<Action> m_mainThreadActions = new ConcurrentQueue<Action>();

        public bool IsConnected => m_client != null && m_client.IsConnected;

        private void Awake()
        {
            if (Instance != null && Instance != this)
            {
                Destroy(gameObject);
                return;
            }
            Instance = this;
            DontDestroyOnLoad(gameObject);

            m_client = new NetworkClient();
            m_client.OnConnected += () => m_mainThreadActions.Enqueue(() => OnConnected?.Invoke());
            m_client.OnDisconnected += reason => m_mainThreadActions.Enqueue(() => OnDisconnected?.Invoke(reason));
        }

        private void OnDestroy()
        {
            if (Instance == this)
            {
                m_client?.Disconnect();
                Instance = null;
            }
        }

        private void Update()
        {
            // 1) 연결/해제 이벤트 처리 (메인 스레드)
            while (m_mainThreadActions.TryDequeue(out Action action))
            {
                try { action(); }
                catch (Exception e) { Debug.LogException(e); }
            }

            // 2) 수신 패킷 디스패치 (메인 스레드)
            if (m_client == null) return;

            while (m_client.RecvQueue.TryDequeue(out RawPacket raw))
            {
                try
                {
                    OnPacketReceived?.Invoke(raw);
                }
                catch (Exception e)
                {
                    Debug.LogException(e); // 한 핸들러 예외가 전체를 멈추지 않게
                }
            }
        }

        // ─── public API ─────────────────────────────────────────────────

        public bool Connect(string ip, int port)
        {
            return m_client.Connect(ip, port);
        }

        public void Disconnect()
        {
            m_client.Disconnect();
        }

        // protobuf 메시지를 직렬화해서 전송
        public void Send<TPacket>(Common.GamePacketId packetId, TPacket message)
            where TPacket : IMessage<TPacket>
        {
            byte[] body = message.ToByteArray();
            m_client.Send((ushort)packetId, body);
        }
    }
}
