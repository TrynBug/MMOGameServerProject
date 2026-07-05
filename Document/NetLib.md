# 개요
서버용 C++ 네트워크 라이브러리 입니다.
- 세션을 관리합니다. 세션은 소켓을 가지고 있습니다.
- IOCP로 네트워크 통신 기능을 제공합니다.
- 패킷 버퍼 pool을 제공합니다.
- 서버는 네트워크 라이브러리를 컴포넌트로 소유하여 사용합니다. 상속받지 않습니다.
- 프로젝트의 출력물은 NetLib.lib 이며, namespace는 netlib 입니다.
- NetLib.h 헤더파일은 다른 프로젝트가 NetLib 라이브러리를 쓰기 위해 include 하는 헤더파일 입니다.

# 다른 프로젝트와의 관계
- ServerBase 프로젝트는 NetLib 라이브러리를 사용하여 클라이언트 통신, 서버간 통신을 구현하고 네트워크 기능의 공용로직을 제공합니다. 그리고 GameServer, GatewayServer 등은 ServerBase 클래스를 상속받아 네트워크 기능을 사용합니다. 그리고 NetLib의 패킷 버퍼 pool에서 패킷버퍼를 할당받아 사용합니다.
- 패킷버퍼에 데이터를 직렬화, 역직렬화 하는 기능은 PacketGenerator 프로젝트(protobuf구현)가 담당합니다.


# 프로그래밍
- 프로그래밍언어
	- C++ 20
- 네트워크 모델
	- TCP, IOCP
- 출력물
	- static 라이브러리

# IoContext 클래스
- IOCP, Worker 스레드, PacketPool 등을 관리합니다. 
- IOCP에 세션을 등록하는 기능을 제공합니다.
- 하나의 프로세스에 보통 IoContext 한 개를 두고, 여러 NetServer, NetClient가 IoContext를 공유해서 사용합니다. IoContext를 공유해서 사용하는 이유는 IOCP, 패킷풀 등은 공유해서 사용하는 것이 효율적이기 때문입니다.
- IoContext의 Worker 스레드는 recv 완료통지를 받았을 때 세션 객체 안의 m_pNetBase 멤버를 통해 세션이 어떤 NetServer 또는 NetClient와 연결되어 있는지 알수 있습니다. 그래서 해당 NetServer 또는 NetClient 에게로 패킷 처리를 넘깁니다.
- Overlapped 구조체는 OVERLAPPED를 확장한 OVERLAPPED_EX를 사용하는데, 여기에는 std::shared_ptr<ISession> 멤버가 있습니다. IoContext의 Worker 스레드는 이 멤버에서 세션을 얻습니다.
- 초기화 파라미터:
	- numConcurrentThread : IOCP 동시실행가능 worker스레드 수 (0 = CPU 코어 수)
	- numWorkerThread : IOCP Worker스레드 수 (0 = CPU 코어 수 * 2)
	- initPacketSize : 패킷 풀 최소 버킷 크기 (bytes)
	- maxPacketSize : 패킷 풀 최대 버킷 크기 (bytes)
 
# INetEventHandler 인터페이스, FuncEventHandler 핸들러
- INetEventHandler 인터페이스는 사용자가 네트워크 이벤트 처리방식을 정의하는 인터페이스 입니다.
- FuncEventHandler는 INetEventHandler를 상속받아서 사용자 함수를 등록할 수 있게 구현한 클래스 입니다.
- 사용자는 INetEventHandler를 상속받는방식, FuncEventHandler를 컴포넌트로 소유하는방식 둘중 하나를 사용하면 됩니다.
```cpp
class INetEventHandler
{
public:
    // Accept 되었을 때 호출. 사용자가 true를 리턴하면 Session 객체 생성, false면 연결 끊음. (NetClient에서는 호출되지 않음)
    virtual bool OnAccept(const ISessionPtr& spSession) = 0;

    // Session 객체가 생성되고 연결이 완료되었을 때 호출
    virtual void OnConnect(const ISessionPtr& spSession) = 0;

    // 패킷 1개를 수신했을 때 호출
    virtual void OnRecv(const ISessionPtr& spSession, const PacketPtr& spPacket) = 0;

    // Send가 완료되었을 때 호출
    virtual void OnSendComplete(const ISessionPtr& spSession) = 0;

    // Session 연결이 끊겼을 때 호출
    virtual void OnDisconnect(const ISessionPtr& spSession) = 0;

    // 오류가 발생했을 때 호출
    virtual void OnLog(LogLevel logLevel, const ISessionPtr& spSession, const std::string& msg) = 0;
};
```

# INetBase 클래스
- NetServer, NetClient 클래스가 상속받는 클래스 입니다.
- 특별한 기능이 있지는 않습니다.

# NetServer 클래스
- accept를 받아야 하는 서버가 네트워크 기능 사용을 위해 컴포넌트로 소유하는 클래스 입니다.
- INetBase를 상속받습니다.
- IoContext의 포인터를 멤버로 가집니다. 네트워크 기능, 패킷풀은 IoContext를 통해 사용합니다.
- Listen Socket, Accept 스레드, 세션 map(key=SessionID, value=shared_ptr<Session>)을 멤버로 가집니다.	
- accept한 소켓에는 SO_LINGER 옵션으로 연결 종료 시 4-way handshake 없이 바로 끊기도록 설정합니다. 
- INetEventHandler 인터페이스를 멤버로 가집니다. 사용자는 NetServer에 INetEventHandler를 등록(하나만 등록가능)해야 이것을 통해 네트워크 이벤트를 받을 수 있습니다.
- 초기화 파라미터:
    - ip : 서버 IP
    - port : 서버 port
    - bUseNagle : Nagle 알고리즘 사용 여부
    - recvBufSize : Session 수신용 링버퍼 크기 (bytes)
    - backlog : listen backlog

# NetClient 클래스
- 다른 서버에 connect 해야하는 서버가 네트워크 기능 사용을 위해 컴포넌트로 소유하는 클래스 입니다.
- INetBase를 상속받습니다.
- IoContext의 포인터를 멤버로 가집니다. 네트워크 기능, 패킷풀은 IoContext를 통해 사용합니다.
- 1개의 connect용 세션을 멤버로 가집니다. Connect 기능을 제공합니다.
- connect가 실패했을 경우 계속해서 재연결을 시도하는 기능을 제공합니다.
- INetEventHandler 인터페이스를 멤버로 가집니다. 사용자는 NetClient에 INetEventHandler를 등록(하나만 등록가능)해야 이것을 통해 네트워크 이벤트를 받을 수 있습니다.
- 초기화 파라미터:
    - bUseNagle : Nagle 알고리즘 사용 여부

# 소켓 옵션 (TCP_NODELAY / Nagle)
- 소켓 옵션은 NetServer/NetClient의 `setSocketOptions(SOCKET)`에서 일괄 설정합니다.
- **Nagle 제어**: 초기화 파라미터 `bUseNagle`로 켜고 끕니다. 내부적으로 `TCP_NODELAY` 옵션으로 구현됩니다. `bUseNagle == false` 이면 TCP_NODELAY가 켜져(Nagle off) 작은 패킷을 즉시 전송합니다.
- **현재 설정 상태**: `bUseNagle`의 기본값은 false 이고(NetConfig 기본값), ServerBase의 클라이언트용 설정과 RegistryClient도 명시적으로 false로 둡니다. 따라서 **모든 서버의 모든 연결에서 Nagle은 꺼져 있고 TCP_NODELAY가 켜져 있는 상태**입니다. (실시간 게임 트래픽의 저지연을 위함)
- **Nagle off로 인한 패킷 수 증가 우려**: NetLib는 send 시 `m_sendQueue`에 쌓인 패킷을 한 번의 WSASend에 최대 `SEND_WSABUF_MAX_SIZE`개까지 묶어 보내는 scatter-gather 방식이라(Session의 Send 참조), 여러 패킷이 한 송신버퍼에서 MSS 단위로 세그먼트화됩니다. 그래서 Nagle을 꺼도 "패킷 1개 = TCP 세그먼트 1개"로 쪼개지지 않아 헤더 오버헤드 증가가 완화됩니다.
- **SO_LINGER**: TCP_NODELAY와 함께 `setSocketOptions()`에서 설정합니다. l_onoff=1, l_linger=0 으로 두어 소켓 종료 시 4-way handshake 없이 RST로 즉시 끊습니다.

# ISession 인터페이스
- NetLib가 사용자에게 노출하는 Session 클래스 입니다.
- Send, Disconnect, GetIP, GetPort 등의 순수가상함수만 가집니다.
- Session의 중요 멤버들을 노출하지 않기 위해 사용됩니다.

# Session 클래스
- ISession 인터페이스를 상속받습니다.
- INetBase 포인터를 멤버로 가집니다. 세션이 속한 Network 객체입니다. INetBase를 통해서 세션의 네트워크 이벤트를 자신이 속한 네트워크에 전달합니다. 세션이 생성될 때 INetBase가 제공되어야 합니다. 
- 소켓, 수신버퍼, 수신용 Overlapped 구조체, 송신버퍼, 송신용 Overlapped 구조체 등을 가집니다.
- 사용자 데이터 슬롯(std::shared_ptr<void> 타입) 멤버를 가집니다. 사용자는 이 멤버를 통해 세션에 데이터를 등록할 수 있습니다.

# OVERLAPPED_EX 구조체
WSARecv, WSASend 할때 이 Overlapped 구조체를 사용합니다.
```cpp
struct OVERLAPPED_EX
{
	OVERLAPPED overlapped;
	IO_TYPE ioType  // recv 인지 send 인지 여부;
	std::shared_ptr<ISession> spSession;   // IO 도중 세션 객체가 사라지는것을 방지하기 위한 멤버
};
```

# Session의 Send, Recv
- Recv
	- Session이 생성될 때 WSARecv 합니다.
	- recv 요청은 1개 시점에 반드시 1개만 존재하도록 합니다.
	- recv 성공 완료통지를 받으면 다시 자동으로 recv를 시작합니다.
	- 수신버퍼는 고정된 크기의 링버퍼로 한다.
		- WSARecv 할 때 WSABUF 2개를 사용하여 링버퍼의 뒤쪽구간, 앞쪽구간 2개를 수신버퍼로 전달함
	- OVERLAPPED_EX 구조체를 사용하기 때문에 IO 도중에 Session객체가 제거되지 않습니다.
	
- Send
	- 사용자가 Session에 패킷 전송을 요청하면, m_sendQueue 에 패킷을 입력합니다. 그런다음 현재 send를 진행중인게 아니라면 WSASend를 시도합니다.
	- WSASend 할 때 m_sendQueue 안의 패킷을 한번에 최대 50개까지 꺼내서 WSABUF[50] 에 담아서 전송합니다.
	- send 요청은 1개 시점에 반드시 1개만 존재하도록 합니다.
	- send 성공 완료통지를 받으면 현재 send를 진행중인게 아니라면 m_sendQueue 를 조사하여 남은 패킷을 다시 최대 50개까지 꺼내서 다시 send 합니다.
	- OVERLAPPED_EX 구조체를 사용하기 때문에 IO 도중에 Session객체가 제거되지 않습니다.
	- Send는 같은 PacketPtr 하나를 여러 세션에 Send해도 안전하며(읽기만 함), 브로드캐스트 시 버퍼 1개를 공유합니다.
	
# 패킷 헤더
```cpp
// 패킷 헤더
#pragma pack(push, 1)
struct PacketHeader
{
    uint16 size;      // 헤더 포함 전체 패킷 크기 (NetLib 사용)
    uint16 type;      // 메시지 ID (서버가 사용. NetLib는 사용안함)
    uint8  flags;     // 패킷 압축, 암호화 등의 플래그
    uint8  reserved;  // 메모리정렬, 추후 확장용 예약 필드
};
#pragma pack(pop)

// 패킷 플래그 비트
namespace PacketFlags
{
    constexpr uint8 None       = 0x00;
    constexpr uint8 Encrypted  = 0x01;  // 암호화됨
    constexpr uint8 Compressed = 0x02;  // 압축됨
    constexpr uint8 Sidecar    = 0x04;  // payload 앞에 Sidecar(부가 데이터)가 있음
}
```

# Sidecar 헤더
```cpp
struct SidecarHeader
{
    uint16 size;      // Sidecar 데이터 크기 (헤더 자체 4바이트는 미포함)
    uint16 reserved;  // 메모리정렬, 추후 확장
};
#pragma pack(pop)
```

- Sidecar 헤더는 패킷 payload 데이터는 그대로 유지하면서 패킷에 추가적인 라우팅 정보를 넣을 때 사용합니다. 게이트웨이↔게임서버 양방향에 모두 쓰입니다.
	- **인바운드(클라→게임)**: 게이트웨이가 원본 클라 패킷에 보낸 유저의 AccountId(int64 1개)를 sidecar로 끼워 게임서버로 relay.
	- **아웃바운드(게임→클라)**: 게임서버가 클라용 패킷에 수신자 AccountId 목록(int64 N개)을 sidecar로 붙여 게이트웨이로 전송 → 게이트웨이가 sidecar를 떼고 대상 클라들에게 전달. (별도 래핑 패킷 없이 직접 전달 — 유니캐스트 N=1, 브로드캐스트 N>1 동일 경로)
- 사용법: PacketHeader의 flags에 PacketFlags::Sidecar를 세팅하고, payload를 memmove로 뒤로 밀어 그 사이에 SidecarHeader와 데이터를 넣습니다. 바이트구조는 `[PacketHeader][SidecarHeader][Sidecar 데이터(size)][payload]` 가 됩니다.
- 패킷을 받는 쪽은 flags & PacketFlags::Sidecar 로 존재 여부를 확인하고 꺼내 씁니다. Sidecar를 넣거나 읽는 기능은 Packet 클래스가 제공합니다.
	- `SetSidecar(data, size)`: payload 뒤에 sidecar를 삽입(payload가 이미 있으면 memmove 발생).
	- `StripSidecar()`: sidecar를 제거해 `[PacketHeader][payload]`로 복원(게이트웨이가 클라에 전달하기 전 사용).
	- `FinalizePacketSize(payloadSize)`: 패킷을 새로 만들 때 sidecar를 **먼저** 깔고(빈 payload라 memmove 없음) payload를 직접 직렬화한 뒤 호출하여 size를 확정. 아웃바운드 빌드 경로의 memmove를 없애는 용도.
- Sidecar는 NetLib의 네트워크 기능에 영향을 미치지 않습니다. 패킷의 size가 늘고 payload 위치가 뒤로 밀릴 뿐이며, NetLib는 오직 패킷의 size와 payload만 신경씁니다.

# 패킷 pool
IoContext는 패킷(패킷버퍼) pool을 제공합니다. 패킷할당 함수의 리턴 타입은 shared_ptr<Packet> 입니다.  
사용자(서버)는 Send할 때 패킷 pool에서 shared_ptr<Packet>을 할당받아 여기에 직렬화 합니다.  
그리고 사용자(서버)는 수신된 데이터를 전달받을 때 shared_ptr<Packet> 형태의 패킷을 전달받아 직접 역직렬화 하여 사용합니다.  

참고로 직렬화/역직렬화 할때는 PacketGenerator 라이브러리의 직렬화/역직렬화 기능을 사용합니다.  

## 버킷 샤딩 (락 경합 완화)
- 각 버킷(크기 클래스)의 freeList는 단일 mutex가 아니라 `kPacketPoolShardCount`개의 샤드(각자 mutex + freeList)로 분할됩니다. 스레드는 라운드로빈으로 배정된 자기 샤드를 우선 사용해 mutex 캐시라인 경합을 분산합니다.
	- 스레드별 샤드 인덱스는 멤버 atomic `m_nextShard`를 1씩 증가시켜 배정(thread_local 캐시). ≤ 샤드 수 만큼의 스레드는 서로 다른 샤드를 보장.
	- **Alloc**: 자기 샤드부터 시도하고 비어있으면 다른 샤드를 순회(다른 스레드가 반납한 패킷 회수), 모두 비면 new.
	- **반납(free)**: free하는 스레드가 아니라 **alloc했던 스레드의 home 샤드**로 되돌립니다. 그래야 그 스레드의 다음 Alloc이 자기 샤드에서 바로 hit하여 cross-shard 스캔이 드물어집니다. (deleter에 alloc 시점 home 샤드 인덱스를 캡처)
- **통계**: `GetStats()`가 버킷별 스냅샷(`BucketStats`)을 반환합니다. capacity, allocCount, freeCount, newCount(pool miss), scanMissCount(스캔 중 빈 샤드 probe 수), held(현재 보유 수), shardHeld[](샤드별 보유 분포). 카운터는 모두 각 샤드 락 안에서만 갱신되어 추가 atomic·공유 캐시라인이 없습니다.

# 패킷 암호화
- 암호화는 넣어야 하는데 아직 개발되지는 않았습니다. (암호화 알고리즘 후보: ChaCha20)