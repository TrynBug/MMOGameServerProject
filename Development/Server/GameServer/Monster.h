#pragma once

#include "pch.h"
#include "ActorObject.h"
#include "BasicStatComponent.h"
#include "IMonsterAI.h"
#include "WaypointMover.h"
#include "GameServerDefine.h"   // k_monsterUpdateIntervalMs (idle 업데이트 주기)

#include "Enum/GameEnum_Stat.h"
#include "Generated/GameData_Monster.h"
#include "MonsterCombatComponent.h"      // 전투(타겟팅 + 스킬 + 캐스트) 컴포넌트

#include <memory>

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
    // 생성자는 파라미터 없음. 빈 객체로 생성한 뒤 Initialize 를 호출해야 한다.
    Monster() = default;
    ~Monster() override = default;

    Monster(const Monster&) = delete;
    Monster& operator=(const Monster&) = delete;

    // objectId 는 외부(스폰 로직)에서 ObjectIdGenerator 로 발급해 전달한다.
    // pMonsterData 는 몬스터 종류 데이터. nullptr 이거나 잘못된 인자면 false 를 리턴한다.
    // 위치는 초기화 후 SetPos/SetYaw 로 설정하고, 두뇌는 SetAI 로 주입한다.
    [[nodiscard]] bool Initialize(int64 objectId, const GameData_Monster* pMonsterData);

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
    bool IsMoving() const override { return m_mover.IsMoving(); }
    void StopMoving();
    // destX/Z 로 이동(throttled repath 포함) + sector 갱신. 매 tick 호출.
    void MoveTo(float destX, float destY, float destZ, int64 deltaMs);

    // 사망 후 시체(corpse) 경과시간을 deltaMs 만큼 누적하고, 유지시간(k_corpseDurationMs)을
    // 넘으면 true 를 리턴한다(=디스폰 타이밍). Stage::updateMonsters 가 사망한 몬스터에게만 매 tick 호출한다.
    bool AdvanceCorpseTimer(int64 deltaMs);

    // ── 회전 ──
    void FaceTarget(const StageObject* pTarget);

    // ── 설정값 조회 ──
    bool  IsRanged()       const { return m_desiredRange > 0.0f; }
    float GetAggroRange()  const { return m_aggroRange; }
    float GetLeashRange()  const { return m_leashRange; }
    float GetDesiredRange()const { return m_desiredRange; }

    // ── 전투(스킬 + 캐스트). 두뇌는 이 컴포넌트를 통해 스킬 선택/시전/사거리 조회한다. ──
    MonsterCombatComponent&       GetCombat()       { return m_combat; }
    const MonsterCombatComponent& GetCombat() const { return m_combat; }

    // ── 적응형 업데이트 주기 (두뇌가 상태 전이 시 호출) ──
    // 관여(Chase/전투) 시 engaged(데이터, 예 100ms)로, 비관여(Idle) 시 idle(500ms)로.
    void SetEngagedTick() { SetUpdateIntervalMs(m_engagedUpdateIntervalMs); }
    void SetIdleTick()    { SetUpdateIntervalMs(k_monsterUpdateIntervalMs); }

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

private:
    // Initialize 에서 호출. 종류 데이터의 기본스탯을 m_statComponent 에 적용한다.
    // 종류 데이터가 없으면 false (초기화 실패로 이어짐).
    bool applyBaseStats();

private:
    // 몬스터 종류 데이터 (소유권 없음, 게임데이터는 로드 후 불변).
    const GameData_Monster* m_pMonsterData = nullptr;

    // 경량 스탯 컴포넌트. 생성자에서 종류 데이터의 기본스탯을 적용한다.
    BasicStatComponent m_statComponent;

    // 교체 가능한 두뇌. SpawnMonster 등이 SetAI 로 주입.
    std::unique_ptr<IMonsterAI> m_ai;

    // 스폰 지점(복귀 기준점). 첫 Update 에서 현재 위치로 1회 캡처.
    bool  m_spawnPointSet = false;
    float m_spawnX = 0.0f;
    float m_spawnY = 0.0f;
    float m_spawnZ = 0.0f;

    // 사망 후 시체 유지 경과시간(ms). MarkDead 이후 Stage::updateMonsters 가 매 tick 누적한다.
    int64 m_corpseElapsedMs = 0;

    // ── AI 설정값 (Initialize 에서 GameData_MonsterAI(AIKey) 로 로드. 데이터 없으면 아래 기본값.) ──
    float m_aggroRange   = 10.0f;   // 이 거리 내 유저 감지 → 추격
    float m_leashRange   = 20.0f;   // 스폰에서 이 거리 초과 → 복귀
    float m_desiredRange = 0.0f;    // 원거리 유지 거리(카이팅). 0 이면 근접. (공격 사거리는 GetMaxAttackRange)
    int64 m_engagedUpdateIntervalMs = 100;   // 타겟 관여 중 업데이트 주기(ms). idle 은 k_monsterUpdateIntervalMs.
    // 이동속도는 MoveSpdTotal 스탯에서 읽는다(GetStatTotal(EStatGroup::MoveSpd)). 버프 자동 반영.

    // ── 전투(스킬 + 캐스트 생애주기) 컴포넌트. owner = this. ──
    MonsterCombatComponent m_combat{this};

    // ── 이동 ──────────────────────────────
    // 경로(waypoint) 기반 이동은 공유 컴포넌트가 담당(Character 와 동일 로직).
    WaypointMover m_mover;
    // 마지막으로 길찾기한 목표점 (throttled repath 판정용).
    bool  m_hasPathTarget = false;
    float m_pathTargetX = 0.0f;
    float m_pathTargetZ = 0.0f;
};

using MonsterPtr  = std::shared_ptr<Monster>;
using MonsterWPtr = std::weak_ptr<Monster>;
