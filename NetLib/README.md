# NetworkLib

C++20 기반 MMORPG용 네트워크 라이브러리.  
Windows IOCP + TCP + Protobuf 를 전제로 설계되었으며, static 라이브러리로 빌드합니다.

## 파일 구성

```
NetworkLib/
├── pch.h / pch.cpp         precompiled header
├── Types.h                 기본 데이터 타입 (CodeConvention 준수)
├── NetConfig.h             초기화 파라미터 구조체
├── PacketHeader.h          6바이트 패킷 헤더 + PacketFlags
├── Packet.h / .cpp         패킷 버퍼 클래스
├── PacketPool.h / .cpp     크기별 bucket 방식의 패킷 풀
├── RingBuffer.h / .cpp     수신용 링버퍼 (WSARecv 2-WSABUF 지원)
├── OverlappedEx.h          OVERLAPPED 확장 구조체
├── ISession.h              Session 외부 인터페이스
├── INetEventHandler.h      이벤트 콜백 인터페이스
├── Session.h / .cpp        Session 구현 (Recv/Send 로직)
├── NetServer.h / .cpp      IOCP/Worker/Accept 관리
├── Crypto.h / .cpp         ChaCha20 암호화 (stub)
└── NetworkLib.h            public 최상위 include 헤더
```

## 빌드 방법 (Visual Studio)

### 프로젝트 설정
- **Configuration Type**: Static library (.lib)
- **C++ Language Standard**: ISO C++20 Standard (/std:c++20)
- **Character Set**: Use Unicode
- **Precompiled Header**: Use (/Yu"pch.h")
  - pch.cpp 파일만 "Create (/Yc)" 로 설정

### 링크 라이브러리 (사용하는 쪽)
- `ws2_32.lib`, `mswsock.lib` 는 pch.h의 `#pragma comment(lib, ...)`로 자동 링크됨.
- ChaCha20을 실제로 사용할 경우 libsodium 연동 필요 (Crypto.cpp 주석 참고).

## 사용 예

```cpp
#include "NetworkLib.h"

// 1) 이벤트 핸들러 작성
class MyHandler : public INetEventHandler
{
public:
    bool OnAccept(ISessionPtr spSession) override
    {
        // 사용자 레벨에서 연결 수락 여부 판단
        return true;
    }

    void OnConnect(ISessionPtr spSession) override
    {
        // 세션 생성됨. 필요한 컨텍스트 연결.
    }

    void OnRecv(ISessionPtr spSession, PacketPtr spPacket) override
    {
        // 게임서버라면 이 시점에 '유저 객체의 메시지 큐에 패킷 삽입' 만.
        // Protobuf 역직렬화는 상위 레이어에서.
    }

    void OnDisconnect(ISessionPtr spSession) override
    {
        // 정리
    }

    void OnLog(LogLevel logLevel, ISessionPtr spSession, const std::string& msg) override
    {
        // 로그
    }
};

// 2) 서버 구동
int main()
{
    NetConfig cfg;
    cfg.ip   = "0.0.0.0";
    cfg.port = 9000;
    cfg.numConcurrentThread = 0;        // 0 = CPU 코어 수
    cfg.numWorkerThread     = 0;        // 0 = CPU 코어 수 * 2
    cfg.bUseNagle           = false;
    cfg.initPacketSize      = 512;
    cfg.maxPacketSize       = 65535;
    cfg.recvBufSize         = 64 * 1024;

    MyHandler handler;
    NetServer server;
    server.SetEventHandler(&handler);

    if (!server.Initialize(cfg))       { return 1; }
    if (!server.StartAccept())         { return 1; }

    // ... 서버 실행 ...

    server.Shutdown();
    return 0;
}

// 3) 패킷 전송
void SendSomething(ISessionPtr spSession, NetServer& server,
                   uint16 msgType, const void* payload, int32 payloadSize)
{
    PacketPtr spPacket = server.GetPacketPool().Alloc(
        static_cast<int32>(sizeof(PacketHeader)) + payloadSize);
    if (spPacket == nullptr) { return; }

    spPacket->WriteData(msgType, PacketFlags::None, payload, payloadSize);
    spSession->Send(spPacket);
}
```

## 주요 설계 결정

### 1. Session 수명 관리
- IO 중 Session이 소멸되는 것을 막기 위해 `OVERLAPPED_EX::spSession` 에 `shared_ptr<ISession>`을 보관.
- IOCP CompletionKey에는 원시 포인터(`Session*`)를 넣지만, Worker 스레드는 **항상 `OVERLAPPED_EX::spSession`을 통해 Session에 접근**합니다. CompletionKey는 진단용.

### 2. Recv
- Session 당 수신버퍼는 고정 크기 **링버퍼**.
- WSARecv 시 `WSABUF[2]`로 링버퍼 뒤쪽/앞쪽 영역을 전달.
- 동일 Session에서 Recv는 항상 1건만 in-flight (atomic flag로 보장).
- 수신 완료 → 패킷 파싱 → `OnRecv` 호출 → 다음 WSARecv 자동 등록.

### 3. Send
- Session 당 `std::queue<PacketPtr>` + `std::mutex`.
- `Send()` 호출 시 큐에 push 후 atomic CAS로 송신 시작 권리 확보.
- WSASend는 큐에서 **최대 50개**를 꺼내 `WSABUF[50]`으로 한 번에 전송.
- 진행 중 패킷들은 `m_sendingPackets`에 보관되어 완료 통지 시 일괄 해제 (→ 풀 반납).
- TODO: `std::mutex + std::queue`는 추후 lock-free queue로 교체 가능. 주석에 표시.

### 4. Packet Pool
- 크기별 **bucket** 방식: `initPacketSize`에서 시작해 2배씩 증가, `maxPacketSize`까지.
- `shared_ptr<Packet>` + **커스텀 deleter** 로 refcount 0 시 자동 반납.
- Shutdown 이후 잔존 shared_ptr가 소멸할 때는 deleter가 일반 delete로 동작 (atomic 플래그 체크).

### 5. 연결 종료
- `CloseSocket()` 은 atomic CAS로 **1회만 실행 보장**.
- `CancelIoEx` + `shutdown(SD_BOTH)` 로 pending IO를 깨움.
- 실제 `closesocket`은 Session dtor에서 호출 → OVERLAPPED IO가 shared_ptr로 보호되는 동안 닫히지 않음.
- `SO_LINGER 0` 설정으로 TIME_WAIT 없이 RST 종료.

### 6. SessionID
- NetServer 내부에서만 의미 있는 단순 증가 `int64` 값.
- 서버전체구조.md의 `[0][Timestamp43][ServerID10][Sequence10]` 비트 레이아웃은 게임 객체 ID용으로, 이 라이브러리와 분리. 상위 레이어가 필요 시 별도 생성.

### 7. 암호화
- `ChaCha20` 인터페이스만 제공, 구현은 stub.
- 실제 배포 전 libsodium 연동 필요. `Crypto.cpp` 상단 주석에 가이드 기재.
- 플래그: `PacketFlags::Encrypted = 0x01`, `PacketFlags::Compressed = 0x02`.
- 키 교환 프로토콜은 상위 레이어 책임 (X25519/ECDH 등).

## 미구현 / TODO

- [ ] `Crypto.cpp` libsodium 연동 (현재 stub).
- [ ] LockfreeQueue — `std::mutex + std::queue` 대신 `moodycamel::ConcurrentQueue` 또는 자체 구현.
- [ ] Packet 압축 (`PacketFlags::Compressed` 플래그만 예약된 상태).
- [ ] Outgoing connection (`Connect`) — 현재는 서버 측 Accept만. 필요 시 `NetClient` 추가.
- [ ] 통계/지표 수집 — 초당 수신/송신 바이트, 평균 패킷 크기 등.

## CodeConvention 준수

- `int32`, `uint16` 등 재정의 타입 사용.
- 클래스명 PascalCase, public 함수 PascalCase, private 함수 camelCase.
- 멤버 변수 `m_` 접두사, static 변수 `sm_`, shared_ptr 변수 `sp`, weak_ptr 변수 `wp`.
- out 파라미터 `out` 접두사.
- BSD 중괄호.
- `using`은 기본 데이터 타입과 스마트포인터 typedef에만 사용.
