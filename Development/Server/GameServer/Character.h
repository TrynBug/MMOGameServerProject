#pragma once

#include "pch.h"
#include "StageObject.h"

#include <vector>

// 전방선언 (User <-> Character 양방향 참조의 한쪽)
class User;
using UserWPtr = std::weak_ptr<User>;

// ─────────────────────────────────────────────────────────────
// Character 클래스
// ─────────────────────────────────────────────────────────────
//
// 유저가 선택해서 플레이 중인 캐릭터. StageObject를 상속받는다.
// 캐릭터 선택이 완료된 후 생성되어, Stage(주로 Town/Field/Dungeon)에 등록된다.
//
// ── 라이프타임 ──
// Stage가 유일한 강한 소유자 (m_objects/m_userObjects에 shared_ptr).
// User는 weak_ptr로만 참조 (Character::GetUser()로 lock해서 사용).
// Character는 User를 weak_ptr로 참조.
//
// ── 데이터 ──
// DataStructures::Character (protobuf) 를 m_protoData로 보관.
// 런타임 상태(좌표/yaw/objectId)는 부모 StageObject 멤버를 진실의 원천으로 한다.
// proto의 hp/level/exp 등 DB 직렬화 대상 필드는 m_protoData 위에서 직접 다룬다.
// DB I/O 시점에 좌표 등을 m_protoData에 동기화한 다음 직렬화 한다.
//
// ── 좌표계 ──
// Unity 와 동일. (X, Y, Z) 3D. Y는 높이, X-Z 가 평면.
// yaw 는 Y축 회전, degree 단위.
//
// ── 이동 (NavMesh 기반) ──
// SetDestination 호출 시 Stage::FindPath 로 waypoint 리스트를 얻어 따라간다.
// 길찾기 실패 시 [목적지] 한 점으로만 채워 직선 이동 fallback.
// MoveNtf 의 dest 는 "최종 목적지" 의미 (waypoint 중간점이 아님).
// 클라는 받은 dest 로 자기 NavMesh 길찾기를 따로 수행하여 같은 경로를 재현한다.
class Character : public StageObject
{
public:
    // protobuf 데이터로부터 생성. character_id를 m_objectId로 사용한다.
    // 좌표/yaw는 m_protoData의 값을 부모 StageObject 멤버로 복사한다.
    explicit Character(const DataStructures::Character& protoData);
    ~Character() override = default;

    Character(const Character&) = delete;
    Character& operator=(const Character&) = delete;

public:
    // ── User 참조 (weak_ptr) ────────────────────────────────────
    void    SetUser(const UserWPtr& wpUser) { m_wpUser = wpUser; }
    UserWPtr GetUserWeak() const            { return m_wpUser; }

    // ── proto 데이터 접근 ───────────────────────────────────────
    // const 접근은 자유롭게.
    const DataStructures::Character& GetProto() const { return m_protoData; }

    // 변경 가능 접근 (hp/level/exp 등을 직접 set 하기 위해).
    // 좌표/yaw는 직접 변경하지 말 것. SetPos/SetYaw 사용.
    DataStructures::Character& GetProtoMutable() { return m_protoData; }

    // ── DB 저장 직전 동기화 ─────────────────────────────────────
    // 런타임 좌표/yaw를 m_protoData에 복사한다. DB 직렬화 직전에 호출.
    void SyncRuntimeToProto();

    // ── 이동 ──────────────────────────────────────────────────────────
    // 좌표계: Unity 와 동일. Y는 높이, X-Z 가 평면. yaw는 Y축 회전, degree.
    // dest 는 "최종 목적지" 의미 (waypoint 중간점이 아님). MoveNtf 송신에 사용.
    bool  IsMoving()    const { return m_isMoving; }
    float GetDestX()    const { return m_destX; }
    float GetDestY()    const { return m_destY; }   // 높이
    float GetDestZ()    const { return m_destZ; }   // 평면 깊이축

    // 목적지 설정 + 이동 시작.
    // Stage::FindPath 로 waypoint 리스트를 얻어 따라가기 시작.
    // 길찾기 실패 시 직선 이동 fallback (waypoint = [목적지] 한 개).
    // 목적지가 현재 위치와 거의 같으면 이동 시작 안 함.
    // yaw 는 첫 waypoint 방향으로 자동 계산.
    void SetDestination(float destX, float destY, float destZ);

    // 즉시 정지 + 현재 위치/yaw 설정. MoveStopReq 처리용.
    // waypoint 리스트도 비운다.
    void StopAt(float posX, float posY, float posZ, float yaw);

    // 매 tick 호출. 현재 waypoint 향해 이동, 도달하면 다음 waypoint, 마지막 도달 시 정지.
    // (X-Z 평면 거리로 도달 판정. Y는 waypoint Y로 직접 보간.)
    // waypoint 도달 시점마다 yaw 재계산.
    // 리턴값: 최종 목적지에 도달했는지 여부 (true면 정지 상태로 전환됨).
    bool Update(int64 deltaMs);

private:
    // 현재 waypoint 를 향해 이동 시작 시점에 yaw 를 갱신.
    // (X-Z 평면 상의 방향. Unity 호환 degree.)
    void faceCurrentWaypoint();

    DataStructures::Character m_protoData;
    UserWPtr                  m_wpUser;

    // ── 이동 상태 ────────────────────────────────────────────
    // m_isMoving = false 면 다른 멤버는 의미 없음 (단, m_destX/Y/Z 는 마지막 정지 위치를 보관).
    bool  m_isMoving = false;

    // 최종 목적지 (SetDestination 인자). MoveNtf 의 dest 필드로 송신.
    float m_destX = 0.0f;
    float m_destY = 0.0f;   // 높이
    float m_destZ = 0.0f;   // 평면 깊이축

    // Waypoint 리스트. (x, y, z) 트리플 순서로 floats * 3N 개.
    // 비어있지 않으면 [0..2] 는 첫 번째 waypoint, [3..5] 는 두 번째, ...
    // 마지막 waypoint 가 최종 목적지에 해당.
    // m_isMoving=true 동안만 의미 있음.
    std::vector<float> m_waypoints;

    // m_waypoints 에서 현재 향해가는 waypoint 인덱스 (트리플 단위, 0-based).
    // 0 이면 m_waypoints[0..2] 를 향함. 도달 시 ++.
    int32 m_curWaypointIdx = 0;
};

using CharacterPtr  = std::shared_ptr<Character>;
using CharacterWPtr = std::weak_ptr<Character>;
