#include "pch.h"
#include "Skills/AreaEffect.h"

#include "Stages/Stage.h"
#include "StageObjects/ActorObject.h"

AreaEffect::AreaEffect(const EffectParams& params)
    : m_params(params)
    , m_nextTickAtMs(params.firstTickDelayMs)
{
}

bool AreaEffect::Update(Stage* pStage, int64 deltaMs)
{
    if (m_expired)
        return true;

    m_elapsedMs += deltaMs;

    // 이번 tick 에 도래한 틱을 모두 발사한다. (긴 deltaMs 로 여러 틱이 한 번에 도래할 수 있음.)
    while (m_elapsedMs >= m_nextTickAtMs)
    {
        // 지속시간(lifetimeMs)을 넘긴 틱은 발사하지 않는다. (periodic 의 마지막 경계 처리.)
        if (m_params.lifetimeMs > 0 && m_nextTickAtMs > m_params.lifetimeMs)
            break;

        fireTick(pStage);

        // 단일 틱(instant) 효과: 1회 발사 후 즉시 종료.
        if (m_params.tickIntervalMs <= 0)
        {
            m_expired = true;
            return true;
        }

        m_nextTickAtMs += m_params.tickIntervalMs;
    }

    // 지속시간 종료 판정.
    if (m_params.lifetimeMs > 0 && m_elapsedMs >= m_params.lifetimeMs)
    {
        m_expired = true;
        return true;
    }

    return false;
}

void AreaEffect::fireTick(Stage* pStage)
{
    // 이동형(Linear)이면 경과시간에 따라 중심이 전진한다. 고정형(Static)이면 origin 그대로.
    const Vector3 pos = CalcEffectPosition(m_params, m_params.dir, m_elapsedMs);

    std::vector<StageObject*> enemies;
    pStage->QueryEnemiesInShape(m_params.casterObjectType, pos, m_params.shape, enemies);

    // QueryEnemiesInShape 는 User/Monster 버킷에서만 후보를 모으므로 모두 ActorObject 파생이다.
    for (StageObject* pEnemy : enemies)
    {
        pStage->ApplyEffectDamage(*static_cast<ActorObject*>(pEnemy), m_params.damageAmount, m_params.casterObjectId,
                                  /*isDuplicate*/ false, m_params.skillKey);
    }
}
