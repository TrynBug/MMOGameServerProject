#pragma once

#include "Types.h"
#include <string>

namespace netlib
{

// IoContext 초기화 파라미터
struct IoContextConfig
{
    int32  numConcurrentThread = 0;     // IOCP 동시실행가능 worker스레드 수 (0 = CPU 코어 수)
    int32  numWorkerThread     = 0;     // IOCP Worker스레드 수 (0 = CPU 코어 수 * 2)

    int32  initPacketSize      = 512;   // 패킷 풀 최소 버킷 크기 (bytes)
    int32  maxPacketSize       = 65535; // 패킷 풀 최대 버킷 크기 (bytes)
};

// NetServer 초기화 파라미터
struct NetServerConfig
{
    std::string ip;                     // Bind IP (예: "0.0.0.0")
    uint16      port          = 0;

    bool        bUseNagle     = false;  // Nagle 알고리즘 사용 여부
    int32       recvBufSize   = 65536;  // Session 수신용 링버퍼 크기 (bytes)

    // 수신한 데이터를 Packet으로 만들 때, 데이터 크기와 관계없이 Packet의 capacity에만 추가로 확보할 공간 크기.
    // Packet에 Sidecar 같은 부가 데이터를 붙여야 할 경우 Packet에 남은 공간이 모자랄 일이 없도록 미리 공간을 확보해놓기 위함이다.
    // 허용되는 원본 패킷의 최대 크기는 maxPacketSize - recvPacketReserveSize가 된다.
    int32       recvPacketReserveSize = 0;

    int32       backlog       = 256;    // listen backlog
};

// NetClient 초기화 파라미터
struct NetClientConfig
{
    bool   bUseNagle     = false;
    int32  recvBufSize   = 65536;

    // 재연결 정책
    bool   bAutoReconnect   = true;
    int32  reconnectIntervalMs = 10000;  // 연결 실패했을 때 재연결 시도 간격(ms)
};

} // namespace netlib
