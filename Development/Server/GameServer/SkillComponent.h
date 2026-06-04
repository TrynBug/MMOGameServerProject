#pragma once

#include "pch.h"
#include "GameServerDefine.h"      // Vector3
#include "Enum/GameEnum_Skill.h"   // ENextSkillOrigin

// 전방선언. 완전타입은 SkillComponent.cpp 에서 include.
class ActorObject;
struct GameData_Skill;

// ─────────────────────────────────────────────────────────────
// SkillComponent — 액터의 스킬 시전 + 페이즈 체인 진행 (스킬/효과 시스템)
// ─────────────────────────────────────────────────────────────
//
// BuffComponent 와 동일 패턴: ActorObject 가 값 멤버로 소유하며, 소속 Stage 의 컨텐츠
// 스레드에서만 접근한다 (Update 포함). 별도 락 없음. owner 가 Character 인지 Monster 인지 모른다.
//
// 한 번에 하나의 시전(체인)만 진행한다. 시전하면 entry 스킬을 발동하고, 이후 NextSkillKey
// 체인을 NextTriggerTiming(OnStart/AfterEnd/AfterDelay) 과 NextOrigin 규칙대로 따라가며
// 후속 페이즈를 시각에 맞춰 발동한다. 각 페이즈는 bake → Stage 의 carrier(투사체/장판)로 라우팅된다.
//
// 미구현(후속 단계):
//   - 쿨타임/마나/액션락 게이팅 (5c): 현재 TryCast 는 무조건 시전(진행 중이면 덮어씀).
//   - 이동 페이즈 실제 처리 (5d): 현재 ActorObject::ApplySkillMovement 훅 호출만 (기본 구현 비어있음).
//   - 대미지 공식: bake 가 DamageCoeff 를 그대로 사용 (시전자 스탯 결합은 TODO).
class SkillComponent
{
public:
    explicit SkillComponent(ActorObject* pOwner) : m_pOwner(pOwner) {}

    SkillComponent(const SkillComponent&) = delete;
    SkillComponent& operator=(const SkillComponent&) = delete;

    // 스킬 시전 시작. entry 페이즈의 origin/dir 은 호출자(시전 패킷 핸들러)가 결정해 넘긴다.
    //   origin : entry 페이즈의 중심/시작 위치 (투사체=시작점, 장판=중심).
    //   dir    : 시전 방향 (정규화된 X-Z). 체인 전체가 공유한다.
    //   seed   : 난수 재현용 (scatter 등). 클라와 공유된 값.
    void TryCast(int64 skillKey, const Vector3& origin, const Vector3& dir, uint32 seed);

    // 매 tick(액터 Update 안에서) 호출. 진행 중인 체인의 도래한 페이즈를 발동한다.
    void Update(int64 deltaMs);

    bool IsCasting() const { return m_active; }

private:
    int64                firePhase(const GameData_Skill& skill, const Vector3& origin);   // 생성된 투사체 effectId 리턴 (투사체 아니면 0)
    Vector3              resolveChainOrigin() const;                               // m_pendingOriginMode 기준
    Vector3              computePhaseEnd(const GameData_Skill& skill, const Vector3& origin) const;
    std::vector<Vector3> computeFanDirs(const Vector3& dir, int32 count, float fanAngleDeg) const;
    Vector3              ownerPos() const;

    // 체인 타이밍에서 "이 페이즈가 끝나는 데까지의 시간". AfterEnd/AfterDelay 의 기준.
    static int64 chainDurationMs(const GameData_Skill& skill);

private:
    ActorObject* m_pOwner = nullptr;

    // ── 진행 중인 시전(체인) 상태 ──
    bool    m_active    = false;
    int64   m_elapsedMs = 0;       // 시전 시작 이후 누적 경과시간
    Vector3 m_castDir;             // 체인 공유 시전 방향
    uint32  m_castSeed  = 0;

    int64 m_pendingSkillKey = 0;   // 다음에 발동할 페이즈 스킬 Key (0 = 더 없음)
    int64 m_pendingFireAtMs = 0;   // 그 페이즈 발동 시각 (m_elapsedMs 기준)

    bool    m_pendingIsEntry = false;   // 다음 발동이 entry 페이즈인가 (origin = 호출자 지정값)
    Vector3 m_pendingFixedOrigin;       // entry 페이즈 origin (호출자 지정)

    // 체인 페이즈 origin 결정용 (직전 페이즈가 지정한 NextOrigin 규칙).
    ENextSkillOrigin m_pendingOriginMode          = ENextSkillOrigin::None;
    float            m_pendingCasterFrontDistance = 0.0f;
    Vector3          m_prevCenter;   // 직전 페이즈 중심 (PrevCenter 용)
    Vector3          m_prevEnd;      // 직전 페이즈 종료 위치 (PrevEnd 용)
};
