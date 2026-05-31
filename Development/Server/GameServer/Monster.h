#pragma once

#include "pch.h"
#include "ActorObject.h"
#include "BasicStatComponent.h"
#include "IMonsterAI.h"                     // 교체 가능한 두뇌(unique_ptr<IMonsterAI>)

#include "Enum/GameEnum_Stat.h"             // EStatGroup
#include "Generated/GameData_Monster.h"     // GameData_Monster

#include <vector>
#include <memory>

// ─────────────────────────────────────────────────────────────
// MonsterSkill : 몬스터가 사용하는 스킬 1개의 런타임 정보
// ─────────────────────────────────────────────────────────────
// 스킬은 "행위 주체(Monster)"가 보유하는 데이터이다 (두뇌 종류와 무관).
// TODO(데이터): 현재는 Monster 생성자에서 하드코딩한다.
//   GameData_Monster 에 스킬 컬럼(스킬ID/사거리/쿨다운/선딜)이 추가되면 거기서 읽어 채운다.
struct MonsterSkill
{
    int32 skillId             = 0;      // 스킬 식별자 (향후 스킬 데이터 키)
    float range               = 0.0f;   // 사용 가능 사거리 (이 거리 이내 타겟)
    int64 cooldownMs          = 0;      // 쿨다운 (ms)
    int64 castTimeMs          = 0;      // 선딜/시전 시간 (ms). 0 이면 즉시 발동.
    int64 remainingCooldownMs = 0;      // 남은 쿨다운 (ms, 런타임)
};

// ─────────────────────────────────────────────────────────────
// Monster 클래스 (행위 주체 + 공유 행동 레이어)
// ─────────────────────────────────────────────────────────────
//
// 게임 런타임에 스폰되는 몬스터. ActorObject 를 상속받는다.
// 캐릭터와 달리 DB 에 저장되지 않으며, 몬스터 종류 게임데이터(GameData_Monster)로부터 생성된다.
//
// ── AI 분리 ──
// 의사결정(FSM/BT 등)은 IMonsterAI 두뇌가 담당하고, Monster 는 두뇌가 호출하는
// "공유 행동 API"(이동/타겟/스킬/사거리 조회 등)와 행위 주체 상태(위치/스탯/스킬/스폰)를 제공한다.
// 두뇌는 SetAI 로 주입하며 언제든 교체 가능하다. (잡몹=FSM, 보스=BT 등)
//
// ── 스탯 ──
// 경량 스탯 컴포넌트(BasicStatComponent)를 사용한다.
// 생성 시 종류 데이터의 (Stat#, StatValue#) 목록을 순회하며 ApplyStat 으로 적용한다.
//
// ── 라이프타임 ──
// Stage 가 강한 소유자(shared_ptr). 스폰 시 ObjectId 는 외부에서 발급해 전달한다.
//
// ── 좌표계 ──
// Unity 와 동일. (X, Y, Z) 3D. Y는 높이, X-Z 가 평면. yaw 는 Y축 회전, degree.
//
// ── 스레드 ──
// 멤버 접근은 소속 Stage 의 컨텐츠 스레드에서만 이루어진다 (Update 포함). 락 없음.
class Monster : public ActorObject
{
public:
    // objectId 는 외부(스폰 로직)에서 ObjectIdGenerator 로 발급해 전달한다.
    // pMonsterData 는 몬스터 종류 데이터(스탯/등급/경험치 등). 호출자가 유효성을 보장한다.
    // 위치는 생성 후 SetPos/SetYaw 로 설정한다. 두뇌는 SetAI 로 주입한다.
    Monster(int64 objectId, const GameData_Monster* pMonsterData);
    ~Monster() override = default;

    Monster(const Monster&) = delete;
    Monster& operator=(const Monster&) = delete;

public:
    // ── 종류 데이터 / 스탯 ─────────────────────────────────────
    const GameData_Monster* GetMonsterData() const { return m_pMonsterData; }

    BasicStatComponent&       GetStat()       { return m_statComponent; }
    const BasicStatComponent& GetStat() const { return m_statComponent; }

    double GetStatTotal(EStatGroup group) const override { return m_statComponent.GetTotal(group); }

    // ActorObject hook: expose stat component base so BuffComponent can ApplyStat/RemoveStat.
    StatComponentBase* GetStatComponent() override { return &m_statComponent; }

    // ── 두뇌(AI) ──────────────────────────────────────────────
    // 두뇌 주입(교체). nullptr 도 허용(=AI 없음, Update 시 행동 안 함).
    void SetAI(std::unique_ptr<IMonsterAI> ai) { m_ai = std::move(ai); }

    // 매 tick(컨텐츠 스레드 전용) 호출. 공통 housekeeping 후 두뇌에 위임한다.
    // Stage::updateMonsters 가 m_monsterObjects 를 순회하며 호출한다.
    void Update(int64 deltaMs) override;

    // ─────────────────────────────────────────────────────────
    // 공유 행동 레이어 (두뇌가 호출)
    // ─────────────────────────────────────────────────────────
    // ── 이동 ──
    bool IsMoving() const { return m_isMoving; }
    void StopMoving();
    // destX/Z 로 이동(throttled repath 포함) + sector 갱신. 매 tick 호출.
    void MoveTo(float destX, float destY, float destZ, int64 deltaMs);

    // 현재 이동 목적지 (MoveNtf 브로드캐스트용). is_moving=false 일 때는 의미 없음.
    float GetDestX() const { return m_destX; }
    float GetDestY() const { return m_destY; }
    float GetDestZ() const { return m_destZ; }

    // 이동 상태 변화(시작/목적지 변경/정지)가 있었는지 소비(읽고 리셋)한다.
    // Stage::updateMonsters 가 매 tick 확인하여 변화가 있을 때만 MoveNtf 를 브로드캐스트한다.
    bool ConsumeMoveStateDirty()
    {
        const bool dirty = m_moveStateDirty;
        m_moveStateDirty = false;
        return dirty;
    }

    // ── 타겟 ──
    // 주변 sector 에서 어그로 범위 내 가장 가까운 유저를 찾아 현재 타겟으로 설정 (없으면 해제).
    void         AcquireTarget();
    // 현재 타겟을 Stage 에서 해소. 사라졌거나 없으면 nullptr.
    StageObject* GetTarget() const;
    bool         HasTarget() const { return m_targetObjectId != 0; }
    void         ClearTarget() { m_targetObjectId = 0; }

    // ── 회전 ──
    void FaceTarget(const StageObject* pTarget);

    // ── 설정값 조회 ──
    bool  IsRanged()       const { return m_desiredRange > 0.0f; }
    float GetAggroRange()  const { return m_aggroRange; }
    float GetLeashRange()  const { return m_leashRange; }
    float GetAttackRange() const { return m_attackRange; }
    float GetDesiredRange()const { return m_desiredRange; }

    // ── 스폰지점 / 복귀 ──
    float GetSpawnX() const { return m_spawnX; }
    float GetSpawnY() const { return m_spawnY; }
    float GetSpawnZ() const { return m_spawnZ; }
    float DistSqFromSpawn() const
    {
        const float dx = GetPosX() - m_spawnX;
        const float dz = GetPosZ() - m_spawnZ;
        return dx * dx + dz * dz;
    }
    // 스폰지점으로 위치를 즉시 스냅 + sector 갱신.
    void SnapToSpawn();

    // ── 스킬 ──
    int32               GetSkillCount() const { return static_cast<int32>(m_skills.size()); }
    const MonsterSkill& GetSkill(int32 index) const { return m_skills[index]; }
    // 사용 가능한(쿨다운 끝 + 사거리 내) 스킬 중 우선순위(목록 순서) 최상의 인덱스. 없으면 -1.
    int32 SelectReadySkill(float distToTarget) const;
    // 해당 스킬의 쿨다운을 시작(remaining = cooldown).
    void  StartSkillCooldown(int32 index);
    // 스킬 발동. TODO(전투): 데미지/투사체 등 실제 효과 (현재 스텁).
    void  ExecuteSkill(int32 index, StageObject* pTarget);

private:
    // 생성자에서 호출. 종류 데이터의 기본스탯을 m_statComponent 에 적용한다.
    void applyBaseStats();

    // 매 tick housekeeping: 스킬 쿨다운 진행.
    void tickSkillCooldowns(int64 deltaMs);

    // ── 이동 내부 구현 (Character 패턴 미러링) ──
    void setDestination(float destX, float destY, float destZ);
    bool updateMovement(int64 deltaMs);   // 최종 목적지 도달 시 true
    void faceWaypoint();

private:
    // 몬스터 종류 데이터 (소유권 없음, 게임데이터는 로드 후 불변).
    const GameData_Monster* m_pMonsterData = nullptr;

    // 경량 스탯 컴포넌트. 생성자에서 종류 데이터의 기본스탯을 적용한다.
    BasicStatComponent m_statComponent;

    // 교체 가능한 두뇌. SpawnMonster 등이 SetAI 로 주입.
    std::unique_ptr<IMonsterAI> m_ai;

    // 현재 타겟 (objectId). 0 = 없음. 매 tick GetTarget 으로 해소(despawn 안전).
    int64 m_targetObjectId = 0;

    // 스폰 지점(복귀 기준점). 첫 Update 에서 현재 위치로 1회 캡처.
    bool  m_spawnPointSet = false;
    float m_spawnX = 0.0f;
    float m_spawnY = 0.0f;
    float m_spawnZ = 0.0f;

    // ── AI 설정값 ─────────────────────────────────────────────
    // TODO(데이터): GameData_Monster 컬럼으로 이동. 지금은 근접 몹 기준 기본값 하드코딩.
    //   원거리 몹: m_desiredRange > 0 + m_attackRange 를 길게. (데이터로 세팅)
    float m_aggroRange   = 10.0f;   // 이 거리 내 유저 감지 → 추격
    float m_leashRange   = 20.0f;   // 스폰에서 이 거리 초과 → 복귀
    float m_attackRange  = 2.0f;    // 이 거리 내 → 공격 가능 (근접 기본값)
    float m_desiredRange = 0.0f;    // 원거리 유지 거리. 0 이면 근접.
    float m_moveSpeed    = 4.0f;    // 유닛/초. TODO(데이터/스탯): 종류 데이터의 이동속도로.

    // ── 스킬 ──────────────────────────────────────────────────
    std::vector<MonsterSkill> m_skills;

    // ── 이동 상태 (Character 미러링) ──────────────────────────
    bool  m_isMoving = false;
    // 이동 상태 변화 플래그. setDestination(시작/목적지변경)·StopMoving(정지)에서 set,
    // ConsumeMoveStateDirty 에서 reset. updateMonsters 가 MoveNtf 브로드캐스트 여부 판단에 사용.
    bool  m_moveStateDirty = false;
    float m_destX = 0.0f;
    float m_destY = 0.0f;
    float m_destZ = 0.0f;
    // Waypoint 리스트. (x, y, z) 트리플 순서로 floats * 3N 개. m_isMoving=true 동안만 의미.
    std::vector<float> m_waypoints;
    int32 m_curWaypointIdx = 0;
    // 마지막으로 길찾기한 목표점 (throttled repath 판정용).
    bool  m_hasPathTarget = false;
    float m_pathTargetX = 0.0f;
    float m_pathTargetZ = 0.0f;
};

using MonsterPtr  = std::shared_ptr<Monster>;
using MonsterWPtr = std::weak_ptr<Monster>;
