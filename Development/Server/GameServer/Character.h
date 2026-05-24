#pragma once

#include "pch.h"
#include "StageObject.h"

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
    bool  IsMoving()    const { return m_isMoving; }
    float GetDestX()    const { return m_destX; }
    float GetDestY()    const { return m_destY; }   // 높이
    float GetDestZ()    const { return m_destZ; }   // 평면 깊이축

    // 목적지 설정 + 이동 시작. yaw는 X-Z 평면상 목적지 방향으로 자동 계산 (degree).
    // 목적지가 현재 위치와 거의 같은 위치면 이동 시작 안 함.
    void SetDestination(float destX, float destY, float destZ);

    // 즉시 정지 + 현재 위치/yaw 설정. MoveStopReq 처리용.
    void StopAt(float posX, float posY, float posZ, float yaw);

    // 매 tick 호출. 목적지 방향으로 직선 이동. 도달 시 정지.
    // (X-Z 평면 상에서 이동. Y는 목적지의 Y로 보간.)
    // 리턴값: 도달했는지 여부 (true면 정지 상태로 전환됨).
    bool Update(int64 deltaMs);

private:
    DataStructures::Character m_protoData;
    UserWPtr                  m_wpUser;

    // 이동 중 여부. true면 m_destX/Y/Z를 목표로 직선 이동.
    // (다음 세션에서 NavMesh waypoint 리스트로 교체 예정)
    bool  m_isMoving = false;
    float m_destX    = 0.0f;
    float m_destY    = 0.0f;   // 높이
    float m_destZ    = 0.0f;   // 평면 깊이축
};

using CharacterPtr  = std::shared_ptr<Character>;
using CharacterWPtr = std::weak_ptr<Character>;
