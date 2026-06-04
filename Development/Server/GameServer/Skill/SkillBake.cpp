#include "pch.h"
#include "Skill/SkillBake.h"

#include "Generated/GameData_Skill.h"   // GameData_Skill 컬럼 접근

EffectParams BakeSkillEffectParams(const GameData_Skill& skill,
                                   EObjectType casterType,
                                   int64 casterObjectId,
                                   const Vector3& origin,
                                   const Vector3& dir,
                                   uint32 seed)
{
    EffectParams p;

    p.casterObjectId   = casterObjectId;
    p.casterObjectType = casterType;
    p.skillKey         = skill.Key;
    p.pSkillData       = &skill;                 // 게임데이터는 불변·영속이라 포인터 보관 안전
    p.onHitSkillKey    = skill.OnHitSkillKey;

    // ── 위치 ──
    p.motion   = skill.EffectMotion;             // 게임데이터 enum 과 동일 타입 (ESkillEffectMotion)
    p.origin   = origin;
    p.dir      = dir;
    p.speed    = static_cast<float>(skill.ProjectileSpeed);
    p.maxRange = static_cast<float>(skill.MaxRange);

    // ── 대미지 ──
    p.damage       = skill.EffectDamage;
    p.damageAmount = skill.DamageCoeff;          // v1: 계수를 그대로. TODO: 시전자 스탯과 결합.

    // ── 틱 스케줄 ──
    p.firstTickDelayMs = static_cast<int32>(skill.FirstTickDelayMs);
    p.tickIntervalMs   = static_cast<int32>(skill.TickIntervalMs);
    p.lifetimeMs       = static_cast<int32>(skill.LifetimeMs);

    // ── 범위 모양 ──
    p.shape.type       = skill.EffectShape;      // 게임데이터 enum 과 동일 타입 (ESkillEffectShape)
    p.shape.radius     = static_cast<float>(skill.Radius);
    p.shape.halfWidth  = static_cast<float>(skill.ObbWidth)  * 0.5f;   // 데이터는 전체 가로/세로
    p.shape.halfLength = static_cast<float>(skill.ObbLength) * 0.5f;
    p.shape.forward    = dir;                    // 정규화된 X-Z (호출자 보장). Circle 에서는 미사용.

    // ── 흩뿌리기 ──
    p.scatterCount       = static_cast<int32>(skill.ScatterCount);
    p.scatterInnerRadius = static_cast<float>(skill.ScatterInnerRadius);
    p.scatterOuterRadius = static_cast<float>(skill.ScatterOuterRadius);

    p.seed = seed;
    return p;
}
