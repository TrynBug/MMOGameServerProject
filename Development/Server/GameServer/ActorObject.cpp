#include "pch.h"
#include "ActorObject.h"

ActorObject::ActorObject()
    : m_buffComponent(this)
    , m_skillComponent(this)
{
}

bool ActorObject::Initialize(int64 objectId, EObjectType objectType)
{
    return StageObject::Initialize(objectId, objectType);
}
