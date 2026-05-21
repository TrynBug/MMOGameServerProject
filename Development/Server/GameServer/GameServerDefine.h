#pragma once

// GameServer 전용 상수, enum, 세션 메타정보 구조체 등 정의

// ── Stage ID (GameData_Stage의 Key와 일치) ───────────────────────
// 게임서버 시작 시 항상 생성되는 고정 Stage들의 ID.
constexpr int64 k_systemStageId = 1;     // 캐릭터 선택창
constexpr int64 k_townStageId   = 100;   // 마을

// ── AOI (Area of Interest) ───────────────────────────────────────
// 캐릭터의 시야 범위. range=1이면 자기 sector 포함 3x3, range=2면 5x5.
// 현재는 모든 캐릭터가 동일 값을 사용한다.
constexpr int32 k_aoiRange = 1;

// ── 캐릭터 이동 ──────────────────────────────────────────────
// 캐릭터 이동 속도 (유닛/초). 향후 GameData_Job으로 이관 예정.
constexpr float k_characterMoveSpeed = 500.0f;

// MoveStopReq 수신 시 클라 위치와 서버 위치의 허용 오차 (유닛).
// 이 범위 내면 클라 위치를 인정, 밖이면 서버 위치로 보정 예정 (보정 패킷은 아직 없음).
constexpr float k_movePositionTolerance = 100.0f;

// 내부서버 세션 추가 데이터
struct InternalSessionMeta
{
    bool       handshakeDone = false;     // Handshake 완료여부
    ServerType peerServerType = ServerType::Unknown;   // 서버 타입
    int32      peerServerId = 0;         // 서버ID
    bool       isConnector = false;      // true: 이 서버가 connect한 세션, false: 이 서버가 accept한 세션
};
