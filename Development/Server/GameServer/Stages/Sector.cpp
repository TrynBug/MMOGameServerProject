#include "pch.h"
#include "Stages/Sector.h"
#include "StageObjects/StageObject.h"

void Sector::AddObject(StageObject* pObject)
{
    if (!pObject)
        return;

    const int64 objectId = pObject->GetObjectId();
    const EObjectType objectType = pObject->GetObjectType();

    switch (objectType)
    {
    case EObjectType::Character:
        m_users[objectId] = pObject;
        break;
    case EObjectType::Monster:
        m_monsters[objectId] = pObject;
        break;
    case EObjectType::Prop:
        m_props[objectId] = pObject;
        break;
    case EObjectType::Drop:
        m_drops[objectId] = pObject;
        break;
    default:
        LOG_WRITE(LogLevel::Warn, std::format("unknown objectType={} objectId={}", static_cast<int>(objectType), objectId));
        break;
    }
}

void Sector::RemoveObject(StageObject* pObject)
{
    if (!pObject)
        return;

    const int64 objectId = pObject->GetObjectId();
    const EObjectType objectType = pObject->GetObjectType();

    switch (objectType)
    {
    case EObjectType::Character:
        m_users.erase(objectId);
        break;
    case EObjectType::Monster:
        m_monsters.erase(objectId);
        break;
    case EObjectType::Prop:
        m_props.erase(objectId);
        break;
    case EObjectType::Drop:
        m_drops.erase(objectId);
        break;
    default:
        LOG_WRITE(LogLevel::Warn, std::format("unknown objectType={} objectId={}", static_cast<int>(objectType), objectId));
        break;
    }
}
