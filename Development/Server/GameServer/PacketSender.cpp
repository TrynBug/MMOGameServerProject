#include "pch.h"
#include "PacketSender.h"
#include "Character.h"   // SendStageLoadCompleteRes / SendStatUpdateNtf 의 Character 멤버 접근(스탯/HP)

// ─────────────────────────────────────────────────────────────
// PacketSender 의 패킷별 송신 구현.
// 모든 함수는 SendToUser(공통 배관, PacketSender.h 템플릿)를 통해 게이트웨이로 전송한다.
// 원래 GameServer.cpp / GameServerBuff.cpp 에 흩어져 있던 Send*** 들을 한 곳으로 모았다.
// ─────────────────────────────────────────────────────────────

void PacketSender::SendStageLoadCompleteRes(int64 userId, EResultCode resultCode, int64 stageId, int32 stageDataKey,
                                            float myPosX, float myPosY, float myPosZ, float myYaw)
{
    GamePacket::StageLoadCompleteRes res;
    res.set_result_code(static_cast<int32>(resultCode));
    res.set_error_msg("");
    res.set_stage_id(stageId);
    res.set_stage_data_key(stageDataKey);
    res.set_my_pos_x(myPosX);
    res.set_my_pos_y(myPosY);
    res.set_my_pos_z(myPosZ);
    res.set_my_yaw(myYaw);

    SendToUser(userId, Common::GAME_PACKET_ID_STAGE_LOAD_COMPLETE_RES, res);

    // 스탯/HP 는 StageLoadCompleteRes 뒤에 보낸다.
    // 클라는 이 패킷 수신 시점에 LocalPlayer 를 활성화/배치하므로(2단계 입장),
    // 그 뒤에 도착해야 스탯 핸들러가 대상 캐릭터를 찾을 수 있다 (TCP 순서 보장).
    // 최대치(StatUpdateNtf)가 현재HP(HpMpNtf)보다 먼저 가야 클라 clamp 가 올바르다.
    UserPtr spUser;
    if (m_safeUsers.Find(userId, spUser) && spUser)
    {
        if (CharacterPtr spCharacter = spUser->GetCurrentCharacter())
        {
            SendStatUpdateNtf(userId, *spCharacter);
            SendHpMpNtf(userId, spCharacter->GetCurHp(), spCharacter->GetCurMp());
        }
    }

    LOG_WRITE(LogLevel::Info, std::format("StageLoadCompleteRes sent. userId={} stageId={} stageKey={} pos=({},{},{}) yaw={}",
        userId, stageId, stageDataKey, myPosX, myPosY, myPosZ, myYaw));
}

void PacketSender::SendStageMoveRes(int64 userId, EResultCode resultCode, const std::string& errorMsg, int32 targetStageDataKey)
{
    GamePacket::StageMoveRes res;
    res.set_result_code(static_cast<int32>(resultCode));
    res.set_error_msg(errorMsg);
    res.set_target_stage_data_key(targetStageDataKey);

    SendToUser(userId, Common::GAME_PACKET_ID_STAGE_MOVE_RES, res);
}

void PacketSender::SendObjectVisibilityNtf(int64 userId,
                                           const std::vector<GamePacket::CharacterSpawnInfo>& characterSpawns,
                                           const std::vector<int64>& despawnIds,
                                           const std::vector<GamePacket::MonsterSpawnInfo>& monsterSpawns)
{
    GamePacket::ObjectVisibilityNtf ntf;
    for (const auto& spawn : characterSpawns)
    {
        *ntf.add_character_spawns() = spawn;
    }

    for (const auto& spawn : monsterSpawns)
    {
        *ntf.add_monster_spawns() = spawn;
    }

    for (int64 id : despawnIds)
    {
        ntf.add_despawn_ids(id);
    }

    SendToUser(userId, Common::GAME_PACKET_ID_OBJECT_VISIBILITY_NTF, ntf);

    LOG_WRITE(LogLevel::Info, std::format("ObjectVisibilityNtf sent. userId={} characterSpawns={} monsterSpawns={} despawns={}",
        userId, characterSpawns.size(), monsterSpawns.size(), despawnIds.size()));
}

void PacketSender::SendMoveNtf(int64 userId, int64 objectId,
                               float posX, float posY, float posZ, float yaw,
                               float destX, float destY, float destZ, bool isMoving)
{
    GamePacket::MoveNtf ntf;
    ntf.set_object_id(objectId);
    ntf.set_pos_x(posX);
    ntf.set_pos_y(posY);
    ntf.set_pos_z(posZ);
    ntf.set_yaw(yaw);
    ntf.set_dest_x(destX);
    ntf.set_dest_y(destY);
    ntf.set_dest_z(destZ);
    ntf.set_is_moving(isMoving);

    SendToUser(userId, Common::GAME_PACKET_ID_MOVE_NTF, ntf);
}

void PacketSender::SendMovePosCorrectNtf(int64 userId, float posX, float posY, float posZ, float yaw)
{
    GamePacket::MovePosCorrectNtf ntf;
    ntf.set_pos_x(posX);
    ntf.set_pos_y(posY);
    ntf.set_pos_z(posZ);
    ntf.set_yaw(yaw);

    SendToUser(userId, Common::GAME_PACKET_ID_MOVE_POS_CORRECT_NTF, ntf);

    LOG_WRITE(LogLevel::Info, std::format("MovePosCorrectNtf sent. userId={} pos=({},{},{}) yaw={}", userId, posX, posY, posZ, yaw));
}

void PacketSender::SendStatUpdateNtf(int64 userId, const Character& character)
{
    GamePacket::StatUpdateNtf ntf;
    ntf.set_object_id(character.GetObjectId());

    // 0 이 아닌 스탯만 담는다.
    character.GetStat().ForEachNonZeroStat([&ntf](EStat stat, double value)
    {
        GamePacket::StatEntry* pEntry = ntf.add_entries();
        pEntry->set_stat(static_cast<int32>(stat));
        pEntry->set_value(value);
    });

    SendToUser(userId, Common::GAME_PACKET_ID_STAT_UPDATE_NTF, ntf);

    LOG_WRITE(LogLevel::Info, std::format("StatUpdateNtf sent. userId={} objectId={} count={}",
        userId, character.GetObjectId(), ntf.entries_size()));
}

void PacketSender::SendHpMpNtf(int64 userId, double curHp, double curMp)
{
    GamePacket::HpMpNtf ntf;
    // objectId 는 현재 본인에게만 보내므로 userId 와 동일한 캐릭터 objectId 를 쓴다.
    // (현재 character_id == objectId == userId 체계. 향후 구분되면 명시 전달로 변경.)
    UserPtr spUser;
    int64 objectId = userId;
    if (m_safeUsers.Find(userId, spUser) && spUser)
    {
        if (CharacterPtr spCharacter = spUser->GetCurrentCharacter())
            objectId = spCharacter->GetObjectId();
    }

    ntf.set_object_id(objectId);
    ntf.set_cur_hp(curHp);
    ntf.set_cur_mp(curMp);

    SendToUser(userId, Common::GAME_PACKET_ID_HP_MP_NTF, ntf);
}

void PacketSender::SendBuffNtf(int64 userId, int64 objectId, int32 buffKey, int32 stackCount, int32 remainTimeMs)
{
    GamePacket::BuffNtf ntf;
    ntf.set_object_id(objectId);
    ntf.set_buff_key(buffKey);
    ntf.set_stack_count(stackCount);
    ntf.set_remain_time_ms(remainTimeMs);

    SendToUser(userId, Common::GAME_PACKET_ID_BUFF_NTF, ntf);
}

void PacketSender::SendBuffRemoveNtf(int64 userId, int64 objectId, int32 buffKey)
{
    GamePacket::BuffRemoveNtf ntf;
    ntf.set_object_id(objectId);
    ntf.set_buff_key(buffKey);

    SendToUser(userId, Common::GAME_PACKET_ID_BUFF_REMOVE_NTF, ntf);
}

void PacketSender::SendSkillDamageNtf(int64 userId, int64 targetObjectId, double damage, bool isDuplicate, double remainingHp)
{
    GamePacket::SkillDamageNtf ntf;
    ntf.set_target_object_id(targetObjectId);
    ntf.set_damage(static_cast<float>(damage));
    ntf.set_is_duplicate(isDuplicate);
    ntf.set_remaining_hp(static_cast<float>(remainingHp));

    SendToUser(userId, Common::GAME_PACKET_ID_SKILL_DAMAGE_NTF, ntf);
}

void PacketSender::SendSkillCastNtf(int64 userId, int64 casterObjectId, int32 skillKey, int64 effectId,
                                    float originX, float originY, float originZ, float dirX, float dirZ, uint32 seed,
                                    float moveDistance)
{
    GamePacket::SkillCastNtf ntf;
    ntf.set_caster_object_id(casterObjectId);
    ntf.set_skill_key(skillKey);
    ntf.set_effect_id(effectId);
    ntf.set_origin_x(originX);
    ntf.set_origin_y(originY);
    ntf.set_origin_z(originZ);
    ntf.set_dir_x(dirX);
    ntf.set_dir_z(dirZ);
    ntf.set_seed(seed);
    ntf.set_move_distance(moveDistance);

    SendToUser(userId, Common::GAME_PACKET_ID_SKILL_CAST_NTF, ntf);
}

void PacketSender::SendObjectDeathNtf(int64 userId, int64 objectId, int64 killerObjectId)
{
    GamePacket::ObjectDeathNtf ntf;
    ntf.set_object_id(objectId);
    ntf.set_killer_object_id(killerObjectId);

    SendToUser(userId, Common::GAME_PACKET_ID_OBJECT_DEATH_NTF, ntf);
}
