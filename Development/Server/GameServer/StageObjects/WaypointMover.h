#pragma once

#include "pch.h"

#include <vector>
#include <span>

// 전방선언
class StageObject;

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
    // logFallback : 직선 폴백 시 Warn 로그 출력 여부. Character=true, Monster=false(잦은 repath 로그 스팸 방지).
    void SetDestination(StageObject& obj, float destX, float destY, float destZ, bool logFallback);

    // 즉시 정지 + waypoint 비움.
    void Stop();

    // 1 tick 전진. moveSpeed(유닛/초)는 호출자가 넘긴다(Character=스탯, Monster=고정값).
    // 최종 목적지에 도달해 이번 tick 에 정지했으면 true.
    bool Update(StageObject& obj, int64 deltaMs, float moveSpeed);

    // 현재 향해가는 waypoint(m_curWaypointIdx)부터 끝까지의 (x,y,z) 트리플 평탄 뷰.
    // 이동 복제(MonsterMoveEntry.waypoints)에서 [현재 권위 위치] 다음에 이어 붙인다. 정지 중이면 빈 span.
    std::span<const float> GetRemainingWaypoints() const;

private:
    // 현재 waypoint 를 향해 yaw 갱신 (X-Z 평면, Unity 호환 degree).
    void faceWaypoint(StageObject& obj);

    // m_isMoving=false 면 다른 멤버는 의미 없음.
    bool               m_isMoving = false;

    // Waypoint 리스트. (x, y, z) 트리플 순서로 floats * 3N 개.
    std::vector<float> m_waypoints;

    // m_waypoints 에서 현재 향해가는 waypoint 인덱스 (트리플 단위, 0-based). 도달 시 ++.
    int32              m_curWaypointIdx = 0;
};
