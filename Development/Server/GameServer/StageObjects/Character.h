#pragma once

#include "pch.h"
#include "StageObjects/ActorObject.h"
#include "Components/CharacterStatComponent.h"
#include "StageObjects/WaypointMover.h"

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
// Stage와 User가 강한 소유자(shared_ptr).
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
// 길찾기 실패 시 이동하지 않는다. off-mesh 면 가장 가까운 walkable 로 스냅백 후 재시도.
// 서버가 권위 위치를 매 tick SnapshotNtf 로 스트리밍한다(클라 본인은 예측+화해, 원격은 보간).
class Character : public ActorObject
{
public:
    // protobuf 데이터로부터 생성. character_id를 m_objectId로 사용한다.
    // 좌표/yaw는 m_protoData의 값을 부모 StageObject 멤버로 복사한다.
    // 생성자는 파라미터 없음. 빈 객체로 생성한 뒤 Initialize 를 호출해야 한다.
    Character() = default;
    ~Character() override = default;

    Character(const Character&) = delete;
    Character& operator=(const Character&) = delete;

    // protoData 로 캐릭터를 초기화한다. character_id 가 유효하지 않으면 false 를 리턴한다.
    [[nodiscard]] bool Initialize(const DataStructures::Character& protoData);

public:
    // ── User 참조 (weak_ptr) ────────────────────────────────────
    void    SetUser(const UserWPtr& wpUser) { m_wpUser = wpUser; }
    UserWPtr GetUserWeak() const            { return m_wpUser; }

    // ── proto 데이터 접근 ───────────────────────────────────────
    // const 접근은 자유롭게.
    const DataStructures::Character& GetProto() const { return m_protoData; }

    // StageObject hook: 이 캐릭터를 소유한 계정 ID. AOI 브로드캐스트(Stage::ForEachUserInAoi)가 사용.
    int64 GetOwnerAccountId() const override { return m_protoData.owner_account_id(); }

    // 변경 가능 접근 (hp/level/exp 등을 직접 set 하기 위해).
    // 좌표/yaw는 직접 변경하지 말 것. SetPos/SetYaw 사용.
    DataStructures::Character& GetProtoMutable() { return m_protoData; }

    // ── 스탯 ──────────────────────────────────────────────────
    // 캐릭터의 모든 스탯을 관리하는 컴포넌트.
    // 생성 시 JobBase 기본스탯이 적용되며, 이후 아이템/버프 등이 ApplyStat/RemoveStat 한다.
    CharacterStatComponent&       GetStat()       { return m_statComponent; }
    const CharacterStatComponent& GetStat() const { return m_statComponent; }

    // ActorObject hook: expose stat component base so BuffComponent can ApplyStat/RemoveStat.
    StatComponentBase* GetStatComponent() override { return &m_statComponent; }

    // ActorObject hooks: notify owner client of buff-driven changes.
    // (stat change -> StatUpdateNtf, DoT/HoT HP change -> HpMpNtf)
    void OnStatsChangedByBuff() override;
    void OnHpChangedByBuff() override;

    // ── 최대 HP / MP (ActorObject 오버라이드) ──────────────
    // 그룹의 Total 스탯을 스탯 컴포넌트에서 읽어 리턴 (ActorObject 오버라이드).
    // GetMaxHp/GetMaxMp 는 ActorObject 가 이 함수 위에서 공통 구현한다.
    double GetStatTotal(EStatGroup group) const override
    {
        const StatGroupInfo* pInfo = GameDataTable_Stat::GetGroupInfo(group);
        if (pInfo == nullptr || pInfo->total == EStat::None)
            return 0.0;
        return m_statComponent.Get(pInfo->total);
    }

    // ── DB 저장 관련 ─────────────────────────────────────
    // 런타임 좌표/yaw를 m_protoData에 복사한다. DB 직렬화 직전에 호출.
    void SyncRuntimeToProto();

    // 이 캐릭터에 대해 진행 중인 DB 코루틴 후속작업이 있는지 여부
    // true 이면 이 캐릭터는 Stage에서 제거/다른 Stage로 이동되지 않아야 한다(DB에 업데이트 후 후속작업이 완료될 때까지 Stage에 남아있어야 하기 때문).
    bool HasPendingAsync() const { return m_pendingAsync > 0; }

    // 진행 중인 코루틴 후속작업 수 증감. AsyncPin(RAII)이 호출한다(직접 호출 금지). Stage 스레드 전용.
    void IncPendingAsync() { ++m_pendingAsync; }
    void DecPendingAsync() { --m_pendingAsync; }

    // ── 이동 ──────────────────────────────────────────────────────────
    // 좌표계: Unity 와 동일. Y는 높이, X-Z 가 평면. yaw는 Y축 회전, degree.
    bool  IsMoving()    const override { return m_mover.IsMoving(); }

    // 클라 이동 입력 화해용. 서버가 마지막으로 처리한 MoveIntentReq.input_seq.
    // SnapshotNtf.ack_input_seq 로 본인에게 되돌려준다(클라가 예측을 replay 화해).
    uint32 GetLastInputSeq() const     { return m_lastInputSeq; }
    void   SetLastInputSeq(uint32 seq) { m_lastInputSeq = seq; }

    // 목적지 설정 + 이동 시작.
    // Stage::FindPath 로 waypoint 리스트를 얻어 따라가기 시작.
    // 길찾기 실패 시 이동하지 않는다(off-mesh 면 스냅백 후 재시도).
    // 목적지가 현재 위치와 거의 같으면 이동 시작 안 함.
    // yaw 는 첫 waypoint 방향으로 자동 계산.
    void SetDestination(float destX, float destY, float destZ);

    // 즉시 정지 + 현재 위치/yaw 설정. MoveIntentReq(STOP) 처리/스폰 배치용.
    // waypoint 리스트도 비운다.
    void StopAt(float posX, float posY, float posZ, float yaw);

    // 매 tick 호출. 현재 waypoint 향해 이동, 도달하면 다음 waypoint, 마지막 도달 시 정지.
    // (X-Z 평면 거리로 도달 판정. Y는 waypoint Y로 직접 보간.)
    // waypoint 도달 시점마다 yaw 재계산.
    // 최종 목적지 도달 시 정지 상태로 전환된다. (이동→정지 전환은 호출자가 IsMoving() 변화로 감지.)
    void Update(int64 deltaMs) override;

    // 스킬 강제이동(5d). ActorObject 훅 override.
    //   durationMs<=0 : 즉시 블링크(끝점 스냅) — 순간이동.
    //   durationMs>0  : duration 동안 ease-out 감속 대시 — 글라이드. (지형 충돌은 v1 무시.)
    // 강제이동 중에는 일반(waypoint) 이동이 중단된다.
    void ApplySkillMovement(const Vector3& dir, float distance, int32 durationMs) override;

    // 스킬 강제이동 진행 중인지. (일반 이동과 별개 상태.)
    bool IsSkillMoving() const { return m_skillMoving; }

    // 시전 액션락 적용(ActorObject 훅 override). Stationary 시전 동안 이동 입력을 막는다.
    // 현재 이동을 즉시 정지시키고 lockMs 동안 잠근다. 카운트다운은 Update 에서 진행.
    void ApplyActionLock(int64 lockMs) override;

    // 시전 액션락 중인지. handleMoveIntentReq 가 이동 입력 무시 판정에 사용.
    bool IsActionLocked() const { return m_actionLockRemainingMs > 0; }

    // ── 자동 리스폰 ──────────────────────────────────────────
    // 사망(IsDead) 중일 때만 Stage::updateCharacters 가 매 tick 호출한다.
    // 사망 후 경과를 누적하고 리스폰 지연을 넘기면 1회 true 를 리턴한 뒤 리셋한다(다음 사망 대비).
    bool AdvanceRespawnTimer(int64 deltaMs);

private:
    // Initialize 에서 호출. JobBase 게임데이터의 기본스탯을 m_statComponent 에 적용한다.
    bool applyJobBaseStats();

    // 스킬 강제이동 1 tick 전진. ease-out 위치 갱신, duration 종료 시 끝점 스냅.
    void advanceSkillMove(int64 deltaMs);

    DataStructures::Character m_protoData;
    UserWPtr                  m_wpUser;

    // 캐릭터 스탯 컴포넌트. 생성자에서 JobBase 기본스탯을 적용한다.
    CharacterStatComponent    m_statComponent;

    // ── 이동 ──────────────────────────────────────────────────
    // 경로(waypoint) 기반 이동은 공유 컴포넌트가 담당(Monster 와 동일 로직).
    WaypointMover m_mover;

    // 마지막으로 처리한 클라 이동 입력 seq (화해용). SnapshotNtf.ack_input_seq 로 송신.
    uint32 m_lastInputSeq = 0;

    // 시전 액션락 잔여시간(ms). >0 이면 이동 입력 무시. Update 에서 deltaMs 만큼 감소.
    int64 m_actionLockRemainingMs = 0;

    // 자동 리스폰 대기시간(ms) 및 사망 후 누적 경과. 사망 시점에 0 에서 시작(부활 시 리셋됨).
    static constexpr int64 k_respawnDelayMs = 5000;   // 사망 5초 후 자동 부활
    int64 m_respawnElapsedMs = 0;

    // ── DB 저장 관련 ─────────────────────────────────────

    // 진행 중인 코루틴 후속작업 수. AsyncPin이 Stage 스레드에서만 증감하므로 atomic/lock 불필요.
    int m_pendingAsync = 0;

    // ── 스킬 강제이동 상태 (5d) ─────────────────────────────────
    // 일반 이동(m_isMoving/waypoint)과 별개. m_skillMoving=true 동안 Update 가 ease-out 으로 전진.
    bool   m_skillMoving         = false;
    float  m_skillMoveStartX     = 0.0f;
    float  m_skillMoveStartY     = 0.0f;
    float  m_skillMoveStartZ     = 0.0f;
    float  m_skillMoveDirX       = 0.0f;   // 정규화 X-Z 방향
    float  m_skillMoveDirZ       = 0.0f;
    float  m_skillMoveDistance   = 0.0f;
    int64  m_skillMoveDurationMs = 0;
    int64  m_skillMoveElapsedMs  = 0;
};

using CharacterPtr  = std::shared_ptr<Character>;
using CharacterWPtr = std::weak_ptr<Character>;
