using System;
using System.Collections.Generic;
using Client.Network;
using Common;
using Google.Protobuf;

namespace Client.Packet
{
    // 패킷 ID -> 핸들러 매핑.
    // 핸들러는 메인 스레드(NetworkManager.Update)에서 호출되므로 Unity 객체 안전.
    //
    // 사용 예:
    //   PacketDispatcher.Instance.Register<LoginRes>(
    //       GamePacketId.LoginRes,
    //       res => Debug.Log($"login result: {res.Success}"));
    public class PacketDispatcher
    {
        public static PacketDispatcher Instance { get; } = new PacketDispatcher();

        // RawPacket 1개를 처리하는 내부 핸들러. (제네릭 핸들러를 감싸는 함수)
        private delegate void RawHandler(RawPacket raw);

        private readonly Dictionary<ushort, RawHandler> m_handlers = new Dictionary<ushort, RawHandler>();

        // 알 수 없는 패킷ID 수신 시 호출되는 콜백 (옵션)
        public Action<ushort> OnUnknownPacket;

        // 핸들러 등록. 같은 ID에 다시 등록하면 덮어씌운다.
        public void Register<TPacket>(GamePacketId packetId, Action<TPacket> handler)
            where TPacket : IMessage<TPacket>, new()
        {
            ushort id = (ushort)packetId;
            MessageParser<TPacket> parser = new MessageParser<TPacket>(() => new TPacket());

            m_handlers[id] = raw =>
            {
                TPacket msg = parser.ParseFrom(raw.Body);
                handler(msg);
            };
        }

        public void Unregister(GamePacketId packetId)
        {
            m_handlers.Remove((ushort)packetId);
        }

        public void Clear()
        {
            m_handlers.Clear();
        }

        // NetworkManager 가 메인 스레드에서 호출.
        // 핸들러 내부 예외는 호출자가 try/catch 로 격리하여 한 핸들러 실패가 전체를 멈추지 않게 한다.
        public void Dispatch(RawPacket raw)
        {
            if (!m_handlers.TryGetValue(raw.Type, out RawHandler handler))
            {
                OnUnknownPacket?.Invoke(raw.Type);
                return;
            }
            handler(raw);
        }
    }
}
