namespace Client.Network
{
    // 수신 스레드가 메인 스레드로 전달하는 raw 패킷.
    // protobuf 역직렬화는 메인 스레드(디스패처)에서 수행한다.
    public class RawPacket
    {
        public ushort Type;
        public byte Flags;
        public byte[] Body;
    }
}
