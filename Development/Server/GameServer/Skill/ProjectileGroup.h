#pragma once

#include "pch.h"
#include "Skill/EffectParams.h"   // EffectParams, CalcEffectPosition, Vector3

// 투사체 직격(클라 hit 보고) 처리 결과.
struct ProjectileHitResult
{
    bool   accepted         = false;  // 이 투사체가 아직 미소비이고 hit 이 유효한가
    double damageMultiplier = 0.0;    // 1.0 = 첫 타격, 0.1 = 중복 타격
    bool   isDuplicate      = false;  // duplication flag (감소된 대미지임을 클라에 알림)
};

// ─────────────────────────────────────────────────────────────
// ProjectileGroup — 1회 시전으로 발사된 투사체 묶음 (스킬/효과 시스템)
// ─────────────────────────────────────────────────────────────
//
// 서버는 투사체를 매 tick 굴리지 않는다. 발사 파라미터 + 방향들만 들고 있다가,
// 클라가 hit 을 보고하면 그 시점 위치를 즉석 역산하여 검증한다 (Stage::OnSkillProjectileHit).
//
// 중복타격 규칙:
//   - 1개 투사체는 1개 타겟만 타격한다 (m_consumed).
//   - 같은 타겟을 이 그룹이 2번째 이상 타격하면 대미지가 10% 로 감소하고 duplication flag 가 선다.
//   - (폭발은 이 규칙 밖. 폭발끼리는 독립 — Stage 핸들러에서 직격 배율만 물려받는다.)
//
// 묶음 단위(그룹)로 관리하는 이유: 위 중복타격 감쇠가 "1회 시전" 단위로 적용되기 때문.
class ProjectileGroup
{
public:
    // params: 발사 파라미터(motion=Linear, speed, maxRange, shape=폭발모양, damageAmount 등).
    // dirs:   투사체별 방향(정규화 X-Z). 부채꼴 전개 결과. 투사체 개수 = dirs.size().
    // spawnTimeMs: Stage 단조 시계 기준 발사 시각.
    void Init(const EffectParams& params, const std::vector<Vector3>& dirs, int64 spawnTimeMs);

    int64               GetEffectId() const { return m_params.effectId; }
    const EffectParams& GetParams()   const { return m_params; }

    // 투사체 projectileIndex 의 nowMs 시점 위치 (검증용). index 가 범위를 벗어나면 origin 을 리턴.
    Vector3 GetProjectilePosition(int32 projectileIndex, int64 nowMs) const;

    // 투사체 projectileIndex 의 발사 방향 (정규화 X-Z). 최대사거리 폭발 위치 계산 등에 사용.
    Vector3 GetDir(int32 projectileIndex) const
    {
        if (projectileIndex < 0 || projectileIndex >= static_cast<int32>(m_dirs.size()))
            return Vector3();
        return m_dirs[projectileIndex];
    }

    // 1투사체 1타겟 + 중복타격 감쇠 bookkeeping. 결과(수락 여부 / 배율 / 중복 여부)를 리턴.
    ProjectileHitResult TryHit(int32 projectileIndex, int64 targetId);

    // 직격 외(최대사거리/지형 도달)로 투사체를 종료 처리한다. 아직 미소비였으면 true + 소비 처리.
    bool TryConsume(int32 projectileIndex);

    bool IsExpired(int64 nowMs) const { return nowMs >= m_expireTimeMs; }

private:
    EffectParams                     m_params;
    std::vector<Vector3>             m_dirs;             // 투사체별 방향 (개수 = 투사체 수)
    std::vector<bool>                m_consumed;         // 투사체별 소비 여부 (1투사체 1타겟)
    std::unordered_map<int64, int32> m_targetHitCount;   // 타겟ID → 이 그룹이 때린 횟수
    int64                            m_spawnTimeMs  = 0;
    int64                            m_expireTimeMs = 0;
};
