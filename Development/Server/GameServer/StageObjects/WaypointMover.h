#pragma once

#include "pch.h"

#include <vector>

// 전방선언
class StageObject;
class Stage;

// ─────────────────────────────────────────────────────────────
// WaypointMover : 경로(waypoint) 기반 이동 컴포넌트
// ─────────────────────────────────────────────────────────────
//
// Character / Monster 가 공유한다(이동 시뮬레이션 중복 제거). 대상 StageObject 의
// 위치/yaw 를 직접 갱신한다(서버 권위). 길찾기는 obj.GetStage()->FindPath 를 사용한다.
//
// 이동 규칙(한 tick 이동량을 while-루프로 소모, 선두 waypoint 스킵, 최종점 스냅+정지)은
// 클라 LocalPlayerMover 와 동일해야 위치가 동기화된다.
//
// 이동속도/스킬강제이동/리패스 throttle 등 "주체별로 다른 것"은 소유자(Character/Monster)가
// 들고 있고, 이 클래스는 순수 경로추적만 담당한다.
class WaypointMover
{
public:
    bool IsMoving() const { return m_isMoving; }

    // 목적지로 경로 설정 + 이동 시작. 목적지가 너무 가깝거나 경로가 없으면 정지 상태가 된다.
    // 길찾기 실패 시 직선 이동하지 않고 정지한다.
    // 단, 대상이 NavMesh 밖(off-mesh)이면 가장 가까운 walkable 로 스냅백한 뒤 한 번 더 시도한다.
    // logMoveFailure : 길찾기 최종 실패 시 Warn 로그 출력 여부. Character=true, Monster=false(잦은 repath 로그 스팸 방지).
    void SetDestination(StageObject& obj, float destX, float destY, float destZ, bool logMoveFailure);

    // 즉시 정지 + waypoint 비움.
    void Stop();

    // 1 tick 전진. moveSpeed(유닛/초)는 호출자가 넘긴다(Character=스탯, Monster=고정값).
    // 최종 목적지에 도달해 이번 tick 에 정지했으면 true.
    bool Update(StageObject& obj, int64 deltaMs, float moveSpeed);

private:
    // 현재 waypoint 를 향해 yaw 갱신 (X-Z 평면, Unity 호환 degree).
    void faceWaypoint(StageObject& obj);

    // 대상이 NavMesh 밖(off-mesh)이면 넓은 박스로 가장 가까운 walkable 위로 위치를 끌어온다.
    // 실제로 옮겼으면 true (그 후 호출자가 길찾기 재시도). 이미 mesh 위거나 근처에 walkable 없으면 false.
    bool trySnapBackOntoNavMesh(StageObject& obj, Stage& stage);

    // m_isMoving=false 면 다른 멤버는 의미 없음.
    bool               m_isMoving = false;

    // Waypoint 리스트. (x, y, z) 트리플 순서로 floats * 3N 개.
    std::vector<float> m_waypoints;

    // m_waypoints 에서 현재 향해가는 waypoint 인덱스 (트리플 단위, 0-based). 도달 시 ++.
    int32              m_curWaypointIdx = 0;
};
