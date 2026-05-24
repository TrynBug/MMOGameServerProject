#include "pch.h"
#include "Character.h"
#include "GameServerDefine.h"   // k_characterMoveSpeed

#include <cmath>

namespace
{
    // 같은 위치 판정 임계값 제곱 (X-Z 평면, 유닛^2).
    // 0.1유닛 이내(=10cm) 이면 이동 안 함.
    constexpr float k_minMoveDistSq = 0.01f;

    // 라디안 -> degree 변환 상수
    constexpr float k_radToDeg = 57.2957795f;   // 180.0f / PI
}

Character::Character(const DataStructures::Character& protoData)
    : StageObject(protoData.character_id(), EObjectType::User)
    , m_protoData(protoData)
{
    // proto의 좌표/yaw를 부모 StageObject 멤버에 복사.
    // (런타임은 StageObject 멤버를 진실의 원천으로 사용한다.)
    SetPos(protoData.pos_x(), protoData.pos_y(), protoData.pos_z());
    SetYaw(protoData.yaw());

    // 이동 상태는 정지로 시작. dest = 현재 위치.
    m_isMoving = false;
    m_destX    = protoData.pos_x();
    m_destY    = protoData.pos_y();
    m_destZ    = protoData.pos_z();
}

void Character::SyncRuntimeToProto()
{
    // DB 직렬화 직전에 호출되어, 런타임 좌표/yaw를 proto에 반영.
    m_protoData.set_pos_x(GetPosX());
    m_protoData.set_pos_y(GetPosY());
    m_protoData.set_pos_z(GetPosZ());
    m_protoData.set_yaw(GetYaw());
}

void Character::SetDestination(float destX, float destY, float destZ)
{
    // X-Z 평면 거리로 이동 여부 판정. Y(높이) 변화는 이동 트리거에 사용하지 않음.
    const float dx = destX - GetPosX();
    const float dz = destZ - GetPosZ();
    const float distSq = dx * dx + dz * dz;

    if (distSq < k_minMoveDistSq)
    {
        m_isMoving = false;
        return;
    }

    m_destX    = destX;
    m_destY    = destY;
    m_destZ    = destZ;
    m_isMoving = true;

    // yaw 계산: X-Z 평면 상의 방향. Unity 와 동일하게 +Z를 정면으로 보고 시계방향(Y축 회전) degree.
    //   Unity: dirY_deg = atan2(dx, dz) * 180/PI
    SetYaw(std::atan2(dx, dz) * k_radToDeg);
}

void Character::StopAt(float posX, float posY, float posZ, float yaw)
{
    SetPos(posX, posY, posZ);
    SetYaw(yaw);
    m_destX    = posX;
    m_destY    = posY;
    m_destZ    = posZ;
    m_isMoving = false;
}

bool Character::Update(int64 deltaMs)
{
    if (!m_isMoving)
        return false;

    // X-Z 평면 거리로 이동 진행도 판정. (Y는 목적지의 Y로 직접 보간)
    const float dx = m_destX - GetPosX();
    const float dz = m_destZ - GetPosZ();
    const float distSq = dx * dx + dz * dz;

    // 이번 tick에 이동할 거리.
    const float moveDist = k_characterMoveSpeed * (static_cast<float>(deltaMs) / 1000.0f);
    const float moveDistSq = moveDist * moveDist;

    if (distSq <= moveDistSq)
    {
        // 이번 tick에 도달. 정확히 목적지로 스냅하고 정지.
        SetPos(m_destX, m_destY, m_destZ);
        m_isMoving = false;
        return true;
    }

    // 도달 전. 목적지 방향(X-Z)으로 moveDist 만큼 이동.
    // Y는 (목적지 - 시작) 비례로 보간 (단순 선형). NavMesh 단계에서 정교화 예정.
    const float dist = std::sqrt(distSq);
    const float nx = dx / dist;
    const float nz = dz / dist;
    const float ratio = moveDist / dist;   // 이번 tick 진행 비율
    const float dy = m_destY - GetPosY();
    SetPos(GetPosX() + nx * moveDist,
           GetPosY() + dy * ratio,
           GetPosZ() + nz * moveDist);
    return false;
}
