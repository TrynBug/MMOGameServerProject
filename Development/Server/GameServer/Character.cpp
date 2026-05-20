#include "pch.h"
#include "Character.h"

Character::Character(const DataStructures::Character& protoData)
    : StageObject(protoData.character_id(), EObjectType::User)
    , m_protoData(protoData)
{
    // proto의 좌표/yaw를 부모 StageObject 멤버에 복사.
    // (런타임은 StageObject 멤버를 진실의 원천으로 사용한다.)
    SetPos(protoData.pos_x(), protoData.pos_y());
    SetYaw(protoData.yaw());
}

void Character::SyncRuntimeToProto()
{
    // DB 직렬화 직전에 호출되어, 런타임 좌표/yaw를 proto에 반영.
    m_protoData.set_pos_x(GetPosX());
    m_protoData.set_pos_y(GetPosY());
    m_protoData.set_yaw(GetYaw());
}
