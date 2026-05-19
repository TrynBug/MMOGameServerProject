using System;

namespace Client.Network
{
    // 서버의 netlib::PacketHeader 와 동일한 6바이트 구조.
    // [size(2)] [type(2)] [flags(1)] [reserved(1)]
    // size = 헤더 포함 전체 패킷 크기.
    // x86_64 Windows 기준 little-endian 으로 직렬화한다.
    public static class PacketHeaderHelper
    {
        public const int HeaderSize = 6;

        // packetType + body 로 [헤더 + body] 전체 바이트 배열을 만든다.
        public static byte[] MakePacket(ushort packetType, byte[] body)
        {
            int bodyLen = body?.Length ?? 0;
            int totalLen = HeaderSize + bodyLen;

            byte[] packet = new byte[totalLen];
            // size
            packet[0] = (byte)(totalLen & 0xFF);
            packet[1] = (byte)((totalLen >> 8) & 0xFF);
            // type
            packet[2] = (byte)(packetType & 0xFF);
            packet[3] = (byte)((packetType >> 8) & 0xFF);
            // flags
            packet[4] = 0;
            // reserved
            packet[5] = 0;

            if (bodyLen > 0)
            {
                Buffer.BlockCopy(body, 0, packet, HeaderSize, bodyLen);
            }
            return packet;
        }

        // 6바이트 헤더를 파싱한다.
        public static void Parse(byte[] headerBuf, out ushort size, out ushort type, out byte flags)
        {
            size = (ushort)(headerBuf[0] | (headerBuf[1] << 8));
            type = (ushort)(headerBuf[2] | (headerBuf[3] << 8));
            flags = headerBuf[4];
            // headerBuf[5] = reserved (사용 안함)
        }
    }
}
