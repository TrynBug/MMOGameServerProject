#include "pch.h"
#include "EventArea.h"

#include <cmath>

bool EventArea::Initialize(int64 objectId, const StageLayout::EventArea& placement)
{
    if (!StageObject::Initialize(objectId, EObjectType::EventArea))
        return false;

    m_eventKey = placement.key;
    m_shape    = placement.shape;
    m_radius   = placement.radius;
    m_sizeX    = placement.sizeX;
    m_sizeZ    = placement.sizeZ;
    m_secure   = placement.secure;
    SetPos(placement.cx, placement.cy, placement.cz);   // 중심 = 위치

    return true;
}

bool EventArea::Contains(float px, float pz, float tolerance) const
{
    const float dx = px - GetPosX();
    const float dz = pz - GetPosZ();

    if (m_shape == 1)   // Box: 중심 기준 X-Z AABB (size = 전체 크기)
    {
        const float halfX = m_sizeX * 0.5f + tolerance;
        const float halfZ = m_sizeZ * 0.5f + tolerance;
        return std::abs(dx) <= halfX && std::abs(dz) <= halfZ;
    }

    // Sphere: 평면 원(X-Z 거리)
    const float r = m_radius + tolerance;
    return (dx * dx + dz * dz) <= (r * r);
}
