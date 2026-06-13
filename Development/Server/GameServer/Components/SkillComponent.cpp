#include "pch.h"
#include "Components/SkillComponent.h"

#include "StageObjects/ActorObject.h"
#include "Stages/Stage.h"
#include "Skills/SkillBake.h"
#include "Generated/GameData_Skill.h"

#include <cmath>

void SkillComponent::TryCast(int32 skillKey, const Vector3& origin, const Vector3& dir, const Vector3& targetPos, uint32 seed)
{
    const GameData_Skill* pSkill = GameDataTable_Skill::FindData(skillKey);
    if (pSkill == nullptr)
        return;

    // 제자리시전(Stationary)은 ActionLockMs 동안 이동을 막는다(서버 권위). 시전 시작 시 즉시 적용.
    // (이동시전 Mobile / 이동기 Mobility 는 이동 허용이라 잠그지 않는다.)
    if (pSkill->CastClass == ESkillCastClass::Stationary && pSkill->ActionLockMs > 0)
        m_pOwner->ApplyActionLock(pSkill->ActionLockMs);

    // TODO(skill 5c): 쿨타임/마나 게이팅. 현재는 무조건 시전(진행 중이면 덮어씀).
    m_active    = true;
    m_elapsedMs = 0;
    m_castDir   = dir;
    m_castTargetPos = targetPos;
    m_castSeed  = seed;

    // entry 페이즈는 선딜레이(CastDelayMs) 후 발동. origin 은 호출자 지정값.
    m_pendingSkillKey   = skillKey;
    m_pendingFireAtMs   = pSkill->CastDelayMs;
    m_pendingIsEntry    = true;
    m_pendingFixedOrigin = origin;
    m_prevCenter        = origin;
    m_prevEnd           = origin;
}

void SkillComponent::Update(int64 deltaMs)
{
    if (!m_active)
        return;

    m_elapsedMs += deltaMs;

    // 도래한 페이즈를 순서대로 발동 (긴 deltaMs 로 여러 페이즈가 한 번에 도래할 수 있음).
    while (m_active && m_pendingSkillKey != 0 && m_elapsedMs >= m_pendingFireAtMs)
    {
        const GameData_Skill* pSkill = GameDataTable_Skill::FindData(m_pendingSkillKey);
        if (pSkill == nullptr)
        {
            m_active = false;   // 데이터 없음 → 체인 중단
            break;
        }

        const bool    isEntry  = m_pendingIsEntry;
        const Vector3 origin   = isEntry ? m_pendingFixedOrigin : resolveChainOrigin();
        const int64   effectId = firePhase(*pSkill, origin);

        // entry 페이즈 발동 시 시전 통보를 AOI 에 broadcast (클라 비주얼 재현 + 시전 클라에 effectId 전달).
        if (isEntry)
        {
            if (Stage* pStage = m_pOwner->GetStage())
                pStage->BroadcastSkillCastNtf(*m_pOwner, pSkill->Key, effectId, origin, m_castDir, m_castSeed, m_lastMoveDistance);
        }

        // 다음 NextOrigin 결정을 위해 이 페이즈의 중심/종료 위치를 기록.
        m_prevCenter = origin;
        m_prevEnd    = computePhaseEnd(*pSkill, origin);

        // 다음 페이즈 스케줄.
        if (pSkill->NextSkillKey != 0 && pSkill->NextTriggerTiming != ENextSkillTiming::None)
        {
            int64 offset = 0;
            switch (pSkill->NextTriggerTiming)
            {
            case ENextSkillTiming::OnStart:    offset = 0; break;
            case ENextSkillTiming::AfterEnd:   offset = chainDurationMs(*pSkill); break;
            case ENextSkillTiming::AfterDelay: offset = chainDurationMs(*pSkill) + pSkill->NextTriggerDelayMs; break;
            default: break;
            }

            m_pendingFireAtMs           += offset;
            m_pendingSkillKey            = pSkill->NextSkillKey;
            m_pendingOriginMode          = pSkill->NextOrigin;
            m_pendingCasterFrontDistance = static_cast<float>(pSkill->CasterFrontDistance);
            m_pendingIsEntry             = false;
        }
        else
        {
            m_pendingSkillKey = 0;   // 체인 끝
        }
    }

    if (m_pendingSkillKey == 0)
        m_active = false;
}

int64 SkillComponent::firePhase(const GameData_Skill& skill, const Vector3& origin)
{
    Stage* pStage = m_pOwner->GetStage();
    if (pStage == nullptr)
        return 0;

    m_lastMoveDistance = 0.0f;   // 이동 페이즈에서만 갱신.

    EffectParams params = BakeSkillEffectParams(skill, m_pOwner->GetObjectType(), m_pOwner->GetObjectId(), origin, m_castDir, m_castSeed);

    switch (skill.EffectDamage)
    {
    case ESkillEffectDamage::ContactHit:   // 투사체 (직격 보고 기반)
    {
        std::vector<Vector3> dirs = computeFanDirs(m_castDir, static_cast<int32>(skill.ProjectileCount), static_cast<float>(skill.FanAngleDeg));
        return pStage->SpawnSkillProjectileGroup(params, dirs);   // 발급된 effectId 리턴
    }
    case ESkillEffectDamage::Area:         // 범위 (서버 주도 틱)
        pStage->SpawnSkillAreaEffect(params);
        return 0;

    case ESkillEffectDamage::None:
    default:
        // 대미지 없는 페이즈 (이동기 등). 이동은 ApplySkillMovement(5d)로 위임.
        //   - 강제 대시(MoveDurationMs>0): 고정 거리(MoveDistance)를 duration 동안 감속 이동(글라이드).
        //   - 즉시 블링크(MoveDurationMs<=0): 타겟까지 거리로 클램프한 거리만큼 즉시 이동(순간이동).
        if (skill.MoveDistance > 0.0)
        {
            float dist;
            if (skill.MoveDurationMs > 0)
            {
                dist = static_cast<float>(skill.MoveDistance);
            }
            else
            {
                const float toTarget = (m_castTargetPos - origin).LengthXZ();
                const float maxDist  = static_cast<float>(skill.MoveDistance);
                dist = (maxDist < toTarget) ? maxDist : toTarget;
            }
            m_lastMoveDistance = dist;
            m_pOwner->ApplySkillMovement(m_castDir, dist, static_cast<int32>(skill.MoveDurationMs));
        }
        return 0;
    }
}

Vector3 SkillComponent::resolveChainOrigin() const
{
    switch (m_pendingOriginMode)
    {
    case ENextSkillOrigin::CasterPos:   return ownerPos();
    case ENextSkillOrigin::CasterFront: return ownerPos() + m_castDir * m_pendingCasterFrontDistance;
    case ENextSkillOrigin::PrevCenter:  return m_prevCenter;
    case ENextSkillOrigin::PrevEnd:     return m_prevEnd;
    default:                            return m_prevCenter;   // None 등은 직전 중심으로 fallback
    }
}

Vector3 SkillComponent::computePhaseEnd(const GameData_Skill& skill, const Vector3& origin) const
{
    if (skill.MoveDurationMs > 0)                          // 이동 페이즈: 이동 끝 지점
        return origin + m_castDir * static_cast<float>(skill.MoveDistance);
    if (skill.EffectMotion == ESkillEffectMotion::Linear)  // 투사체/직선: 최대사거리 끝
        return origin + m_castDir * static_cast<float>(skill.MaxRange);
    return origin;                                         // 고정 효과: 시작=끝
}

int64 SkillComponent::chainDurationMs(const GameData_Skill& skill)
{
    if (skill.MoveDurationMs > 0)
        return skill.MoveDurationMs;
    // Area: 단일 틱이면 firstTickDelay 까지, periodic 이면 lifetime 까지가 효과 지속.
    return (skill.FirstTickDelayMs > skill.LifetimeMs) ? skill.FirstTickDelayMs : skill.LifetimeMs;
}

std::vector<Vector3> SkillComponent::computeFanDirs(const Vector3& dir, int32 count, float fanAngleDeg) const
{
    std::vector<Vector3> dirs;
    if (count <= 1)
    {
        dirs.push_back(dir);
        return dirs;
    }

    dirs.reserve(count);

    const float totalRad = fanAngleDeg * 0.01745329f;          // deg → rad
    const float step     = totalRad / static_cast<float>(count - 1);
    const float startRad = -totalRad * 0.5f;

    for (int32 i = 0; i < count; ++i)
    {
        const float a  = startRad + step * static_cast<float>(i);
        const float cs = std::cos(a);
        const float sn = std::sin(a);
        // X-Z 평면에서 dir 을 a 만큼 회전.
        dirs.push_back(Vector3(dir.x * cs - dir.z * sn, dir.y, dir.x * sn + dir.z * cs));
    }
    return dirs;
}

Vector3 SkillComponent::ownerPos() const
{
    return Vector3(m_pOwner->GetPosX(), m_pOwner->GetPosY(), m_pOwner->GetPosZ());
}
