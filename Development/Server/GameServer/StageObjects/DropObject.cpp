#include "pch.h"
#include "StageObjects/DropObject.h"

bool DropObject::Initialize(int64 objectId, int32 itemKey, int32 count,
                            int64 ownerAccountId, int64 ownerCharacterId,
                            float originX, float originY, float originZ,
                            int64 createdServerTimeMs, int64 expireServerTimeMs)
{
    // 런타임 object ID/타입 등록은 공통 베이스가 담당한다. 아래 값들은 패킷과
    // 소유권 검증에 직접 쓰이므로 생성 시점에 불변 조건을 한 번 확인한다.
    if (!StageObject::Initialize(objectId, EObjectType::Drop))
        return false;
    if (itemKey <= 0 || count <= 0 || ownerAccountId <= 0 || ownerCharacterId <= 0 ||
        expireServerTimeMs <= createdServerTimeMs)
        return false;

    m_itemKey = itemKey;
    m_count = count;
    m_ownerAccountId = ownerAccountId;
    m_ownerCharacterId = ownerCharacterId;
    m_originX = originX;
    m_originY = originY;
    m_originZ = originZ;
    m_createdServerTimeMs = createdServerTimeMs;
    m_expireServerTimeMs = expireServerTimeMs;
    return true;
}
