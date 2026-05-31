#include "pch.h"
#include "ActorObject.h"

ActorObject::ActorObject(int64 objectId, EObjectType objectType)
    : StageObject(objectId, objectType)
    , m_buffComponent(this)
{
}
