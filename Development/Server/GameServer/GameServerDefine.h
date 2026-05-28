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

// MoveStopReq 수신 시 클라 위치와 서버 위치의 허용 오차 (X-Z 평면 거리, 유닛).
// MoveDestReq/MoveStopReq 둘 다 이 값을 사용.
// 이 범위 내면 클라 위치 인정, 초과면 MovePosCorrectNtf 송신.
//
// 값 결정 근거:
//   - 이동 방향이 바뀌면 클라가 200ms 주기로 더 자주 경로를 다시 잡는 동안 서버는 500ms 동안 옷 경로를
//     따라가기 때문에 1~1.5m 정도의 누적 오차가 자연스럽게 발생.
//   - 3.0m 면 이런 정상 플레이에서는 통과, 텍일포트 수준의 이상 이동만 감지.
//   - 향후 이동중 vs 정지시 동적 tolerance, 속도 기반 검증 등으로 개선 예정.
constexpr float k_movePositionTolerance = 3.0f;

// 내부서버 세션 추가 데이터
struct InternalSessionMeta
{
    bool       handshakeDone = false;     // Handshake 완료여부
    ServerType peerServerType = ServerType::Unknown;   // 서버 타입
    int32      peerServerId = 0;         // 서버ID
    bool       isConnector = false;      // true: 이 서버가 connect한 세션, false: 이 서버가 accept한 세션
};
