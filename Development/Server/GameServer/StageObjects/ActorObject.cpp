#include "pch.h"
#include "StageObjects/ActorObject.h"

ActorObject::ActorObject()
    : m_buffComponent(this)
    , m_skillComponent(this)
{
}

bool ActorObject::Initialize(int64 objectId, EObjectType objectType)
{
    return StageObject::Initialize(objectId, objectType);
}

bool ActorObject::MarkDead(int64 killerObjectId)
{
    if (m_isDead)
        return false;
    m_isDead = true;
    m_killerObjectId = killerObjectId;
    return true;
}


void ActorObject::MarkAlive()
{
    m_isDead = false;
    m_killerObjectId = 0;
}

double ActorObject::clampToRange(double value, double maxValue)
{
    const double hi = (maxValue > 0.0) ? maxValue : 0.0;
    if (value < 0.0)   return 0.0;
    if (value > hi)    return hi;
    return value;
}