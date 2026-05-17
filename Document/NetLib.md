# 개요
MMORPG용 C++ 네트워크 라이브러리 입니다.
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

# 네트워크 라이브러리의 초기화 파라미터
- IP
- Port
- NumConcurrentThread : IOCP 동시실행 가능한 worker 스레드 수
- NumWorkerThread: 생성할 IOCP Worker 스레드 수
- bUseNagle : Nagle 알고리즘 사용여부
- InitPacketSize : 패킷버퍼 초기 크기
- MaxPacketSize : 패킷버퍼가 늘어날 수 있는 최대 크기
- RecvBufSize : 소켓 수신버퍼(링버퍼) 고정크기

# NetServer 클래스
- IOCP, Worker 스레드를 소유합니다.
- Accept 스레드를 소유합니다.
- Session을 관리합니다.
	- key=SessionID, value=shared_ptr<Session> 인 map으로 관리합니다.
	
- Initialize 함수
	- IOCP, Worker 스레드 생성
	- Worker 스레드 시작
- StartAccept 함수
	- Accept 소켓을 생성하고, Accept 스레드를 시작하고, Accept를 시작합니다.
- StopAccept 함수
	- Accept를 중지합니다.
- Shutdown 함수
	- Accept를 중지합니다.
	- 모든 Session의 연결을 끊습니다.
	- 모든 Worker 스레드를 종료합니다.
	- IOCP를 닫습니다.
	
- INetEventHandler 인터페이스 등록 함수
	- INetEventHandler 등록(하나만 등록가능)
 
# INetEventHandler 인터페이스
- INetEventHandler 인터페이스는 사용자가 네트워크 이벤트 처리방식을 정의하는 인터페이스 입니다.
```cpp
class INetEventHandler {
public:
    virtual ~INetEventHandler() = default;
	
	// Accept 되었을때 호출됩니다. 사용자가 true를 리턴하면 네트워크 라이브러리는 Session객체를 생성합니다. 사용자가 false를 리턴하면 연결을 끊습니다.
    virtual bool OnAccept(std::shared_ptr<ISession> spSession) = 0;
	
	// Session 객체가 생성되었을 때 호출됩니다.
    virtual void OnConnect(std::shared_ptr<ISession> spSession) = 0;
	
	// 패킷 1개를 수신했을 때 호출됩니다. 게임서버는 이 함수에 '유저객체의 메시지큐에 패킷 삽입' 로직만 작성할 예정입니다.
    virtual void OnRecv(std::shared_ptr<ISession> spSession, shared_ptr<Packet> spPacket) = 0;
	
	// Session 연결이 끊겼을때 호출됩니다.
    virtual void OnDisconnect(std::shared_ptr<ISession> spSession) = 0;
	
	// 오류가 발생했을 때 호출됩니다.
    virtual void OnError(std::shared_ptr<ISession> spSession, const std::string& msg) = 0;
};
```

# ISession 인터페이스
- 사용자에게 노출하는 Session객체 입니다.
- Send, Disconnect, GetIP, GetPort 등의 순수가상함수만 가집니다.
```cpp
class ISession {
public:
    virtual ~ISession() = default;
	// 게임서버가 호출할 수 있는 최소한의 메서드만
    virtual void     Send(const Packet& pkt) = 0;
    virtual void     Disconnect() = 0;
    virtual int64_t  GetId() const = 0;
};
```

# Session 클래스
- ISession 인터페이스를 상속받습니다.
```cpp
// 라이브러리 내부 구현 (게임서버에는 안 보임)
class Session : public ISession {
    SOCKET            m_socket;
    RingBuffer        m_recvBuf;     // 수신용 링버퍼
	OVERLAPPED_EX     m_recvOverlapped; // 수신용 Overlapped 구조체
    LockfreeQueue<std::shared_ptr<Packet>>   m_sendQueue;  // 보낼 패킷 큐
	OVERLAPPED_EX     m_sendOverlapped; // 송신용 Overlapped 구조체
    // ... 진짜 구현 디테일
public:
    void Send(std::shared_ptr<Packet> spPacket) override    { /* ... */ }
    void Disconnect() override                { /* ... */ }
    int64_t GetId() const override            { /* ... */ }
};
```


# IOCP
- CompletionKey에는 세션 포인터를 입력합니다.

# 소켓 옵션
- SO_LINGER 옵션으로 연결 종료 시 4-way handshake 없이 바로 끊기도록 합니다.

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
	
# 패킷 헤더
```cpp
#pragma pack(push, 1)
struct PacketHeader {
    uint16_t size;      // 헤더 포함 전체 패킷 크기 (라이브러리가 해석)
    uint16_t type;      // 메시지 ID (상위 레이어가 해석, 라이브러리는 투과)
    uint8_t  flags;     // 전송 레벨 플래그 (압축, 암호화, ...)
    uint8_t  reserved;  // 정렬 및 확장 여유
};
#pragma pack(pop)
static_assert(sizeof(PacketHeader) == 6);
```

# 패킷 pool
네트워크 라이브러리는 패킷(패킷버퍼) pool을 제공합니다.  
리턴 타입은 shared_ptr<Packet> 입니다.  
사용자(게임서버)는 Send할 때 패킷 pool에서 shared_ptr<Packet>을 할당받아 여기에 직렬화 합니다.  
그리고 OnRecv 함수로 shared_ptr<Packet> 형태의 패킷을 전달받아 사용자(게임서버)가 역직렬화 하여 사용합니다.  

참고로 패킷 구조체는 protobuf로 생성되며, PacketGenerator 프로젝트가 protobuf 기능을 담당합니다.  
직렬화/역직렬화 할때는 PacketGenerator 라이브러리의 직렬화/역직렬화 기능을 사용합니다.  

# 패킷 암호화
- ChaCha20 알고리즘으로 패킷을 암호화 한다.