#include "pch.h"
#include "GameServer.h"

// ─────────────────────────────────────────────────────────────
// 버프 뱃지 송신 (GameServer 멤버)
// ─────────────────────────────────────────────────────────────
//
// 버프 패킷은 UI 뱃지 데이터(어떤 버프가 몇 스택, 얼마나 남았는지)만 담는다.
// 스탯 변화는 StatUpdateNtf, DoT/HoT HP 변화는 HpMpNtf 로 별도 전송된다.
// 호출 경로: BuffComponent -> Stage::BroadcastBuffNtf/RemoveNtf -> (AOI 유저별) 여기.
//
// 거대한 GameServer.cpp 와 분리해 이 파일에 둔다. (다른 Send* 와 동일하게 sendPacketToUser 사용.)

void GameServer::SendBuffNtf(int64 userId, int64 objectId, int64 buffKey, int32 stackCount, int32 remainTimeMs)
{
    GamePacket::BuffNtf ntf;
    ntf.set_object_id(objectId);
    ntf.set_buff_key(buffKey);
    ntf.set_stack_count(stackCount);
    ntf.set_remain_time_ms(remainTimeMs);

    sendPacketToUser(userId, Common::GAME_PACKET_ID_BUFF_NTF, ntf);
}

void GameServer::SendBuffRemoveNtf(int64 userId, int64 objectId, int64 buffKey)
{
    GamePacket::BuffRemoveNtf ntf;
    ntf.set_object_id(objectId);
    ntf.set_buff_key(buffKey);

    sendPacketToUser(userId, Common::GAME_PACKET_ID_BUFF_REMOVE_NTF, ntf);
}
