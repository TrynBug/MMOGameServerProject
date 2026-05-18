#pragma once

// GameServer 전용 상수, enum, 세션 메타정보 구조체 등 정의

// 컨텐츠 스레드 인덱스
// 0번 스레드는 오픈필드 전용, 1번부터는 일반 컨텐츠(공용던전/유저던전 등)
constexpr int32 k_openFieldThreadIndex = 0;

// 오픈필드 Stage ID (고정)
constexpr int64 k_openFieldStageId = 1;

// 내부서버 세션 추가 데이터
struct InternalSessionMeta
{
    bool       handshakeDone = false;     // Handshake 완료여부
    ServerType peerServerType = ServerType::Unknown;   // 서버 타입
    int32      peerServerId = 0;         // 서버ID
    bool       isConnector = false;      // true: 이 서버가 connect한 세션, false: 이 서버가 accept한 세션
};
