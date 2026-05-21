#include "pch.h"
#include "Character.h"
#include "GameServerDefine.h"   // k_characterMoveSpeed

#include <cmath>

Character::Character(const DataStructures::Character& protoData)
    : StageObject(protoData.character_id(), EObjectType::User)
    , m_protoData(protoData)
{
    // proto의 좌표/yaw를 부모 StageObject 멤버에 복사.
    // (런타임은 StageObject 멤버를 진실의 원천으로 사용한다.)
    SetPos(protoData.pos_x(), protoData.pos_y());
    SetYaw(protoData.yaw());

    // 이동 상태는 정지로 시작. dest = 현재 위치.
    m_isMoving = false;
    m_destX    = protoData.pos_x();
    m_destY    = protoData.pos_y();
}

void Character::SyncRuntimeToProto()
{
    // DB 직렬화 직전에 호출되어, 런타임 좌표/yaw를 proto에 반영.
    m_protoData.set_pos_x(GetPosX());
    m_protoData.set_pos_y(GetPosY());
    m_protoData.set_yaw(GetYaw());
}

void Character::SetDestination(float destX, float destY)
{
    const float dx = destX - GetPosX();
    const float dy = destY - GetPosY();
    const float distSq = dx * dx + dy * dy;

    // 같은 위치(또는 거의 같은 위치)면 이동 안 함. yaw는 갱신 안 함.
    constexpr float k_minMoveDistSq = 0.01f;   // 0.1유닛 이내면 무시
    if (distSq < k_minMoveDistSq)
    {
        m_isMoving = false;
        return;
    }

    m_destX    = destX;
    m_destY    = destY;
    m_isMoving = true;

    // yaw는 목적지 방향으로 자동 계산. atan2(dy, dx) → 라디안.
    SetYaw(std::atan2(dy, dx));
}

void Character::StopAt(float posX, float posY, float yaw)
{
    SetPos(posX, posY);
    SetYaw(yaw);
    m_destX    = posX;
    m_destY    = posY;
    m_isMoving = false;
}

bool Character::Update(int64 deltaMs)
{
    if (!m_isMoving)
        return false;

    const float dx = m_destX - GetPosX();
    const float dy = m_destY - GetPosY();
    const float distSq = dx * dx + dy * dy;

    // 이번 tick에 이동할 거리.
    const float moveDist = k_characterMoveSpeed * (static_cast<float>(deltaMs) / 1000.0f);
    const float moveDistSq = moveDist * moveDist;

    if (distSq <= moveDistSq)
    {
        // 이번 tick에 도달. 정확히 목적지로 스냅하고 정지.
        SetPos(m_destX, m_destY);
        m_isMoving = false;
        return true;
    }

    // 도달 전. 목적지 방향으로 moveDist 만큼 이동.
    const float dist = std::sqrt(distSq);
    const float nx = dx / dist;
    const float ny = dy / dist;
    SetPos(GetPosX() + nx * moveDist, GetPosY() + ny * moveDist);
    return false;
}
