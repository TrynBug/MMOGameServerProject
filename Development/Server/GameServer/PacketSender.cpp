#include "pch.h"
#include "PacketSender.h"
#include "StageObjects/Character.h"

// ─────────────────────────────────────────────────────────────
// PacketSender 의 패킷별 송신 구현.
// 모든 함수는 SendToUser(공통 배관, PacketSender.h 템플릿)를 통해 게이트웨이로 전송한다.
// ─────────────────────────────────────────────────────────────

// 직렬화된 payload 를 클라용 패킷 [Header(packetType)][payload] 로 만들고, 수신자 userId 목록을
// sidecar 로 붙여 게이트웨이로 전송한다. userId 수가 많아 패킷 최대크기(0xFFFF)를 넘으면 분할한다.
void PacketSender::sendClientPacketViaGateway(const netlib::ISessionPtr& spGatewaySession, int32 packetType,
                                              const std::string& payload, const int64* userIds, int32 userIdCount)
{
    const int32 headerSize  = static_cast<int32>(sizeof(netlib::PacketHeader));
    const int32 sidecarHdr  = static_cast<int32>(sizeof(netlib::SidecarHeader));
    const int32 payloadSize = static_cast<int32>(payload.size());

    // payload 가 고정이므로 한 패킷에 담을 수 있는 userId 개수를 미리 계산한다.
    const int32 maxSidecarBytes = 0xFFFF - headerSize - payloadSize - sidecarHdr;
    int32 maxIdsPerPacket = maxSidecarBytes / static_cast<int32>(sizeof(int64));
    if (maxIdsPerPacket < 1)
        maxIdsPerPacket = 1;   // payload 가 비정상적으로 큰 경우에도 루프 진행을 보장 (SetSidecar 에서 실패 로깅)

    for (int32 offset = 0; offset < userIdCount; offset += maxIdsPerPacket)
    {
        const int32 count        = (maxIdsPerPacket < userIdCount - offset) ? maxIdsPerPacket : (userIdCount - offset);
        const int32 sidecarBytes = count * static_cast<int32>(sizeof(int64));
        const int32 totalCapacity = headerSize + payloadSize + sidecarHdr + sidecarBytes;

        netlib::PacketPtr spPacket = m_server.GetIoContext().GetPacketPool().Alloc(totalCapacity);
        if (!spPacket)
        {
            LOG_WRITE(LogLevel::Error, std::format("packet pool alloc failed. packetType={} size={}", packetType, totalCapacity));
            return;
        }

        // [Header(packetType)][payload] 구성 후 수신자 userId 목록을 sidecar 로 붙인다.
        spPacket->SetHeader(static_cast<uint16>(headerSize), static_cast<uint16>(packetType), netlib::PacketFlags::None);
        if (payloadSize > 0 && !spPacket->WritePayload(payload.data(), payloadSize))
        {
            LOG_WRITE(LogLevel::Error, std::format("WritePayload failed. packetType={} payloadSize={}", packetType, payloadSize));
            return;
        }
        if (!spPacket->SetSidecar(userIds + offset, sidecarBytes))
        {
            LOG_WRITE(LogLevel::Error, std::format("SetSidecar failed. packetType={} count={}", packetType, count));
            return;
        }

        spGatewaySession->Send(spPacket);
    }
}

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

void PacketSender::SendSnapshotNtf(int64 userId, const GamePacket::SnapshotNtf& ntf)
{
    // 고빈도(매 tick) 패킷이라 로그를 남기지 않는다. ntf 는 Stage 가 AOI 순회로 채워 전달한다.
    SendToUser(userId, Common::GAME_PACKET_ID_SNAPSHOT_NTF, ntf);
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

void PacketSender::SendBuffNtf(const std::vector<int64>& userIds, int64 objectId, int32 buffKey, int32 stackCount, int32 remainTimeMs)
{
    GamePacket::BuffNtf ntf;
    ntf.set_object_id(objectId);
    ntf.set_buff_key(buffKey);
    ntf.set_stack_count(stackCount);
    ntf.set_remain_time_ms(remainTimeMs);

    SendToUsers(userIds, Common::GAME_PACKET_ID_BUFF_NTF, ntf);
}

void PacketSender::SendBuffRemoveNtf(const std::vector<int64>& userIds, int64 objectId, int32 buffKey)
{
    GamePacket::BuffRemoveNtf ntf;
    ntf.set_object_id(objectId);
    ntf.set_buff_key(buffKey);

    SendToUsers(userIds, Common::GAME_PACKET_ID_BUFF_REMOVE_NTF, ntf);
}

void PacketSender::SendSkillDamageNtf(const std::vector<int64>& userIds, int64 targetObjectId, double damage, bool isDuplicate, double remainingHp,
                                      int64 attackerObjectId, int32 sourceSkillKey)
{
    GamePacket::SkillDamageNtf ntf;
    ntf.set_target_object_id(targetObjectId);
    ntf.set_damage(static_cast<float>(damage));
    ntf.set_is_duplicate(isDuplicate);
    ntf.set_remaining_hp(static_cast<float>(remainingHp));
    ntf.set_attacker_object_id(attackerObjectId);
    ntf.set_source_skill_key(sourceSkillKey);

    SendToUsers(userIds, Common::GAME_PACKET_ID_SKILL_DAMAGE_NTF, ntf);
}

void PacketSender::SendAbilityCastNtf(const std::vector<int64>& userIds, int64 casterObjectId, int32 skillKey, int64 targetObjectId,
                                      float originX, float originY, float originZ, float dirX, float dirZ, int32 windupMs)
{
    GamePacket::AbilityCastNtf ntf;
    ntf.set_caster_object_id(casterObjectId);
    ntf.set_skill_key(skillKey);
    ntf.set_target_object_id(targetObjectId);
    ntf.set_origin_x(originX);
    ntf.set_origin_y(originY);
    ntf.set_origin_z(originZ);
    ntf.set_dir_x(dirX);
    ntf.set_dir_z(dirZ);
    ntf.set_windup_ms(windupMs);

    SendToUsers(userIds, Common::GAME_PACKET_ID_ABILITY_CAST_NTF, ntf);
}

void PacketSender::SendSkillCastNtf(const std::vector<int64>& userIds, int64 casterObjectId, int32 skillKey, int64 effectId,
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

    SendToUsers(userIds, Common::GAME_PACKET_ID_SKILL_CAST_NTF, ntf);
}

void PacketSender::SendObjectDeathNtf(const std::vector<int64>& userIds, int64 objectId, int64 killerObjectId)
{
    GamePacket::ObjectDeathNtf ntf;
    ntf.set_object_id(objectId);
    ntf.set_killer_object_id(killerObjectId);

    SendToUsers(userIds, Common::GAME_PACKET_ID_OBJECT_DEATH_NTF, ntf);
}

void PacketSender::SendStageNoticeNtf(const std::vector<int64>& userIds, const std::string& message, int32 durationMs)
{
    GamePacket::StageNoticeNtf ntf;
    ntf.set_message(message);
    ntf.set_duration_ms(durationMs);

    SendToUsers(userIds, Common::GAME_PACKET_ID_STAGE_NOTICE_NTF, ntf);
}
