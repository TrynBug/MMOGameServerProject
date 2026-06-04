#pragma once

#include "pch.h"
#include "GameServerDefine.h"        // Vector3
#include "Enum/GameEnum_Skill.h"     // ESkillEffectShape (게임데이터 생성 enum)

// ─────────────────────────────────────────────────────────────
// EffectShape — 스킬/효과의 범위 모양 (X-Z 평면)
// ─────────────────────────────────────────────────────────────
//
// 범위 판정은 모두 X-Z 평면에서 한다 (높이 Y 는 무시). 게임이 쿼터뷰 평면 기반이라
// 대미지 범위는 평면 모양으로 충분하다.
//
// 중심 좌표(center)는 모양에 넣지 않는다. 이동하는 효과는 매 tick center 가 달라지므로
// center 는 판정 시점에 인자로 받고, 모양 자체(반지름/크기/방향)만 보관한다.
//
// 모양 종류 enum 은 게임데이터 enum(ESkillEffectShape)을 그대로 사용한다.

struct EffectShape
{
    ESkillEffectShape type = ESkillEffectShape::None;

    // Circle 용 반지름.
    float radius = 0.0f;

    // Obb 용. forward 축 길이의 절반(halfLength), 그에 수직인 축 길이의 절반(halfWidth).
    // 예) 블레이즈 "전방 가로 5, 세로 50" → halfWidth = 2.5, halfLength = 25.
    float halfWidth  = 0.0f;
    float halfLength = 0.0f;

    // Obb 의 forward 방향 (정규화된 X-Z 벡터, y 는 0). Circle 에서는 사용하지 않는다.
    // 시전 시 클라가 보낸 시전 방향에서 채운다.
    Vector3 forward;

    // point 가 (center 를 중심으로 한) 이 모양 안에 있는지 판정한다. X-Z 평면 기준.
    bool Contains(const Vector3& center, const Vector3& point) const
    {
        const Vector3 d = point - center;

        switch (type)
        {
        case ESkillEffectShape::Circle:
            return d.LengthSqXZ() <= radius * radius;

        case ESkillEffectShape::Obb:
        {
            // d 를 forward 축과 그에 수직인 right 축에 투영하여 박스 범위 안인지 검사한다.
            // forward 는 단위벡터라고 가정. right 는 forward 를 X-Z 평면에서 90도 돌린 방향이며,
            // 대칭 범위 검사(±half)라 right 의 부호/방향은 결과에 영향을 주지 않는다.
            const float projForward = d.x * forward.x + d.z * forward.z;
            const float projRight   = d.x * forward.z - d.z * forward.x;

            return projForward >= -halfLength && projForward <= halfLength
                && projRight   >= -halfWidth  && projRight   <= halfWidth;
        }
        default:
            return false;
        }
    }

    // 이 모양이 center 로부터 X-Z 평면상 닿을 수 있는 최대 반경.
    // QueryEnemiesInShape 가 검사할 후보 섹터 범위를 보수적으로 추려낼 때 사용한다.
    float GetBoundingRadiusXZ() const
    {
        switch (type)
        {
        case ESkillEffectShape::Circle:
            return radius;
        case ESkillEffectShape::Obb:
            return std::sqrt(halfWidth * halfWidth + halfLength * halfLength);
        default:
            return 0.0f;
        }
    }
};
