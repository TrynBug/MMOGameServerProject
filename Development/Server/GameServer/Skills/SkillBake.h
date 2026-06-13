#pragma once

#include "pch.h"
#include "Skills/EffectParams.h"
#include "Enum/GameEnum_Common.h"

struct GameData_Skill;

// ─────────────────────────────────────────────────────────────
// GameData_Skill → EffectParams 변환 (bake)
// ─────────────────────────────────────────────────────────────
//
// 스킬 게임데이터의 컬럼을 런타임 효과 파라미터로 굽는다. 두 경로가 공용으로 사용한다:
//   - 시전 경로 (SkillComponent): 유저/AI 가 스킬을 시전할 때.
//   - 폭발 경로 (Stage::OnSkillProjectileHit): 투사체 hit 시 OnHitSkillKey 스킬을 폭발로 발동할 때.
//
// origin / dir / seed 는 호출자가 상황에 맞게 결정해 넘긴다 (bake 는 위치 정책을 모른다).
//   - dir: 정규화된 X-Z 벡터를 기대한다 (Obb forward / 투사체 방향에 그대로 쓰임).
//
// 대미지(damageAmount)는 v1 에서 DamageCoeff 를 그대로 사용한다.
// TODO(skill): 대미지 공식 확정 시 시전자 스탯과 결합하도록 변경.
EffectParams BakeSkillEffectParams(const GameData_Skill& skill,
                                   EObjectType casterType,
                                   int64 casterObjectId,
                                   const Vector3& origin,
                                   const Vector3& dir,
                                   uint32 seed);
