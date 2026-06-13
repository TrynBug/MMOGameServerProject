#pragma once

#include "pch.h"
#include "GameServerDefine.h"
#include "Skills/EffectShape.h"
#include "Enum/GameEnum_Common.h"
#include "Enum/GameEnum_Skill.h"

// 전방선언: 스킬 게임데이터. 여기서는 포인터 멤버로만 보관하므로 정의 없이도 컴파일된다.
struct GameData_Skill;

// ─────────────────────────────────────────────────────────────
// 효과 데이터 모델 (스킬/효과 시스템)
// ─────────────────────────────────────────────────────────────
//
// 스킬의 행동을 직교하는 세 축으로 표현한다: 위치 거동(motion) × 대미지 방식(damage) × 모양(shape).
// 세 축의 enum 은 모두 게임데이터 생성 enum(ESkillEffect*)을 그대로 사용한다.
//
// 스킬 시전 시 스킬데이터 + 시전자 스탯 + 클라 입력으로 EffectParams 를 bake 하고,
// 런타임 carrier(ProjectileGroup / AreaEffect)는 이 값만 들고 동작한다.

// 효과 1개 인스턴스의 파라미터. (bake 결과물)
struct EffectParams
{
    int64       effectId        = 0;                  // 효과 오브젝트 ID. Stage 가 spawn 시 GameServer 로 발급.
    int64       casterObjectId  = 0;                  // 시전자 오브젝트 ID
    EObjectType casterObjectType = EObjectType::None; // 진영 판정용 (시전자 타입). 시전자가 사라져도 안전하도록 bake.

    int32                 skillKey   = 0;             // 스킬 게임데이터 Key
    const GameData_Skill* pSkillData = nullptr;       // 스킬 게임데이터 (불변, 빠른 참조).
    int32                 onHitSkillKey = 0;          // 투사체 hit/최대사거리 도달 시 발동할 폭발 스킬 Key. 없으면 0.

    // ── 위치 ──
    ESkillEffectMotion motion = ESkillEffectMotion::Static;
    Vector3            origin;                         // 효과 시작/중심 위치
    Vector3            dir;                            // 진행 방향 (Linear). 정규화된 X-Z.
    float              speed    = 0.0f;               // Linear 속도 (units/sec)
    float              maxRange = 0.0f;               // 최대 사거리 (ProjectileGroup 에서 사용)

    // ── 대미지 ──
    ESkillEffectDamage damage       = ESkillEffectDamage::None;
    double             damageAmount = 0.0;            // 1회 대미지 (스탯 계산 결과를 구워넣음)

    // ── 틱 스케줄 (Area) ──
    int32 firstTickDelayMs = 0;                       // 첫 틱까지의 지연
    int32 tickIntervalMs   = 0;                       // 틱 간격. <=0 이면 단일 틱(instant).
    int32 lifetimeMs       = 0;                        // 지속시간. <=0 이면 단일 틱 효과에서만 사용.

    // ── 범위 모양 ──
    EffectShape shape;

    // ── 분산(scatter) ──
    // scatterCount > 1 이면 spawn 시 origin 기준 [inner, outer] 링 영역에 시드 랜덤으로 N개로 펼쳐진다 (메테오 파편).
    int32 scatterCount       = 0;
    float scatterInnerRadius = 0.0f;
    float scatterOuterRadius = 0.0f;

    uint32 seed = 0;                                  // 난수 재현용 (클라와 공유)
};

// 경과시간(spawn 이후 ms)에 따른 효과 위치를 계산한다. 상태 저장 없이 즉석 계산한다.
// dir 을 따로 받는 이유: 투사체 부채꼴처럼 인스턴스마다 방향이 다를 수 있어서(ProjectileGroup).
// AreaEffect 는 params.dir 을 그대로 넘긴다.
inline Vector3 CalcEffectPosition(const EffectParams& params, const Vector3& dir, int64 elapsedMs)
{
    switch (params.motion)
    {
    case ESkillEffectMotion::Linear:
    {
        const float elapsedSec = static_cast<float>(elapsedMs) / 1000.0f;
        return params.origin + dir * (params.speed * elapsedSec);
    }
    case ESkillEffectMotion::Static:
    case ESkillEffectMotion::None:
    default:
        return params.origin;
    }
}
