#include "pch.h"
#include "Skill/ProjectileGroup.h"

namespace
{
    // 비행 종료 후에도 늦게 도착하는 hit 보고를 받아주기 위한 여유 시간.
    constexpr int64 k_projectileExpireMarginMs = 1000;
}

void ProjectileGroup::Init(const EffectParams& params, const std::vector<Vector3>& dirs, int64 spawnTimeMs)
{
    m_params      = params;
    m_dirs        = dirs;
    m_spawnTimeMs = spawnTimeMs;

    m_consumed.assign(dirs.size(), false);
    m_targetHitCount.clear();

    // 비행시간 = 최대사거리 / 속도. speed<=0 이면 비행하지 않는 것으로 보고 여유시간만 둔다.
    int64 flightMs = 0;
    if (m_params.speed > 0.0f)
        flightMs = static_cast<int64>((m_params.maxRange / m_params.speed) * 1000.0f);

    m_expireTimeMs = spawnTimeMs + flightMs + k_projectileExpireMarginMs;
}

Vector3 ProjectileGroup::GetProjectilePosition(int32 projectileIndex, int64 nowMs) const
{
    if (projectileIndex < 0 || projectileIndex >= static_cast<int32>(m_dirs.size()))
        return m_params.origin;

    const int64 elapsedMs = nowMs - m_spawnTimeMs;
    return CalcEffectPosition(m_params, m_dirs[projectileIndex], elapsedMs);
}

ProjectileHitResult ProjectileGroup::TryHit(int32 projectileIndex, int64 targetId)
{
    ProjectileHitResult result;

    if (projectileIndex < 0 || projectileIndex >= static_cast<int32>(m_consumed.size()))
        return result;        // 잘못된 index → accepted=false

    if (m_consumed[projectileIndex])
        return result;        // 이미 다른 적을 친 투사체 (1투사체 1타겟)

    m_consumed[projectileIndex] = true;

    const int32 prevCount = m_targetHitCount[targetId];   // 없으면 0
    m_targetHitCount[targetId] = prevCount + 1;

    result.accepted = true;
    if (prevCount == 0)
    {
        result.damageMultiplier = 1.0;
        result.isDuplicate      = false;
    }
    else
    {
        result.damageMultiplier = 0.1;   // 두 번째 타격부터 10%
        result.isDuplicate      = true;
    }
    return result;
}

bool ProjectileGroup::TryConsume(int32 projectileIndex)
{
    if (projectileIndex < 0 || projectileIndex >= static_cast<int32>(m_consumed.size()))
        return false;

    if (m_consumed[projectileIndex])
        return false;   // 이미 종료된 투사체

    m_consumed[projectileIndex] = true;
    return true;
}
