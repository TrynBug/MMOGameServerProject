#pragma once

#include "pch.h"
#include "StageObjects/StageObject.h"

// 몬스터 사망으로 생성되는 개인 소유 필드 드롭
//
// - Stage/sector에 등록되는 AOI 오브젝트지만 드롭의 소유자 account + character 에게만 spawn/despawn한다.
// - GetPos*는 서버가 확정한 착지점이고 origin은 클라이언트 비산 연출의 시작점이다.
class DropObject : public StageObject
{
public:
    bool Initialize(int64 objectId, int32 itemKey, int32 count,
                    int64 ownerAccountId, int64 ownerCharacterId,
                    float originX, float originY, float originZ,
                    int64 createdServerTimeMs, int64 expireServerTimeMs);

    int32 GetItemKey() const { return m_itemKey; }
    int32 GetCount() const { return m_count; }
    int64 GetOwnerAccountId() const override { return m_ownerAccountId; }
    int64 GetOwnerCharacterId() const { return m_ownerCharacterId; }
    float GetOriginX() const { return m_originX; }
    float GetOriginY() const { return m_originY; }
    float GetOriginZ() const { return m_originZ; }
    int64 GetCreatedServerTimeMs() const { return m_createdServerTimeMs; }
    int64 GetExpireServerTimeMs() const { return m_expireServerTimeMs; }
    bool IsExpired(int64 serverTimeMs) const { return serverTimeMs >= m_expireServerTimeMs; }
    // DB 저장이 진행되는 동안 동일 드롭을 다시 처리하거나 만료시키지 않기 위한 in-flight 잠금.
    // Stage 단일 스레드에서만 읽고 쓰므로 atomic은 필요하지 않다.
    bool IsPicking() const { return m_isPicking; }
    void SetPicking(bool value) { m_isPicking = value; }

private:
    int32 m_itemKey = 0;
    int32 m_count = 0;
    int64 m_ownerAccountId = 0;
    int64 m_ownerCharacterId = 0;
    float m_originX = 0.0f;
    float m_originY = 0.0f;
    float m_originZ = 0.0f;
    int64 m_createdServerTimeMs = 0;
    int64 m_expireServerTimeMs = 0;
    bool m_isPicking = false;
};

using DropObjectPtr = std::shared_ptr<DropObject>;
