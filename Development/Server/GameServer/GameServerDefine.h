#pragma once

// GameServer 전용 상수, enum, 세션 메타정보 구조체 등 정의

// ── Stage ID (GameData_Stage의 Key와 일치) ───────────────────────
// 게임서버 시작 시 항상 생성되는 고정 Stage들의 ID.
constexpr int64 k_systemStageId = 1;     // 캐릭터 선택창
constexpr int64 k_townStageId   = 100;   // 마을

// 내부서버 세션 추가 데이터
struct InternalSessionMeta
{
    bool       handshakeDone = false;     // Handshake 완료여부
    ServerType peerServerType = ServerType::Unknown;   // 서버 타입
    int32      peerServerId = 0;         // 서버ID
    bool       isConnector = false;      // true: 이 서버가 connect한 세션, false: 이 서버가 accept한 세션
};
