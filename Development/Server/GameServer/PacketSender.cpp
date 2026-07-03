#include "pch.h"
#include "PacketSender.h"
#include "StageObjects/Character.h"

#ifdef _DEBUG
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <vector>
#include <algorithm>
#include <utility>

// [개발 계측] 패킷 타입별 송신량(클라 수신 기준 header+payload × 수신자수) 집계 + 주기 자동 덤프.
// 대역폭 최적화(예: ActorStateInfo 양자화, 송신율 조정) 전후 비교용. 2초마다 [PacketStats] 로그로 덤프.
namespace packetstats
{
    namespace
    {
        struct Entry { uint64 count = 0; uint64 bytes = 0; };

        std::mutex                            g_mutex;
        std::unordered_map<int32, Entry>      g_byType;
        std::chrono::steady_clock::time_point g_windowStart = std::chrono::steady_clock::now();
        constexpr int64                       k_dumpPeriodMs = 2000;   // 2초마다 자동 덤프.

        // g_mutex 보유 상태에서 호출. 누적값을 바이트 내림차순으로 로그 + 리셋 + window 갱신.
        void dumpLocked(std::chrono::steady_clock::time_point now)
        {
            const double elapsedSec =
                std::chrono::duration_cast<std::chrono::milliseconds>(now - g_windowStart).count() / 1000.0;
            if (elapsedSec <= 0.0)
                return;

            std::vector<std::pair<int32, Entry>> rows(g_byType.begin(), g_byType.end());
            std::sort(rows.begin(), rows.end(),
                      [](const auto& a, const auto& b) { return a.second.bytes > b.second.bytes; });

            uint64 totalBytes = 0;
            uint64 totalCount = 0;
            std::string body;
            for (const auto& [type, e] : rows)
            {
                totalBytes += e.bytes;
                totalCount += e.count;
                const char* name = Common::GamePacketId_IsValid(type)
                    ? Common::GamePacketId_Name(static_cast<Common::GamePacketId>(type)).c_str()
                    : "UNKNOWN";
                body += std::format("\n  {:<42} {:>8} pkt {:>12} B  {:>10.1f} B/s",
                                    name, e.count, e.bytes, static_cast<double>(e.bytes) / elapsedSec);
            }

            LOG_WRITE(LogLevel::Info, std::format("[PacketStats] window={:.1f}s  total={} pkt {} B ({:.1f} B/s){}",
                                                  elapsedSec, totalCount, totalBytes,
                                                  static_cast<double>(totalBytes) / elapsedSec, body));

            g_byType.clear();
            g_windowStart = now;
        }
    }

    void Record(int32 packetType, int32 bytesPerRecipient, int32 recipientCount)
    {
        if (recipientCount <= 0)
            return;

        std::lock_guard<std::mutex> lock(g_mutex);
        Entry& e = g_byType[packetType];
        e.count += static_cast<uint64>(recipientCount);
        e.bytes += static_cast<uint64>(bytesPerRecipient) * static_cast<uint64>(recipientCount);

        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - g_windowStart).count() >= k_dumpPeriodMs)
            dumpLocked(now);
    }
}
#endif

// ─────────────────────────────────────────────────────────────
// PacketSender 의 패킷별 송신 구현.
// 모든 함수는 SendToUser(공통 배관, PacketSender.h 템플릿)를 통해 게이트웨이로 전송한다.
// ─────────────────────────────────────────────────────────────

void PacketSender::SendStageLoadCompleteRes(int64 accountId, EResultCode resultCode, int64 stageId, int32 stageDataKey,
                                            float myPosX, float myPosY, float myPosZ, float myYaw,
                                            float worldMinX, float worldMinZ, float worldMaxX, float worldMaxZ)
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
    res.set_world_min_x(worldMinX);
    res.set_world_min_z(worldMinZ);
    res.set_world_max_x(worldMaxX);
    res.set_world_max_z(worldMaxZ);

    SendToUser(accountId, Common::GAME_PACKET_ID_STAGE_LOAD_COMPLETE_RES, res);

    // 스탯/HP 는 StageLoadCompleteRes 뒤에 보낸다.
    // 클라는 이 패킷 수신 시점에 LocalPlayer 를 활성화/배치하므로(2단계 입장),
    // 그 뒤에 도착해야 스탯 핸들러가 대상 캐릭터를 찾을 수 있다 (TCP 순서 보장).
    // 최대치(StatUpdateNtf)가 현재HP(HpMpNtf)보다 먼저 가야 클라 clamp 가 올바르다.
    UserPtr spUser;
    if (m_safeUsers.Find(accountId, spUser) && spUser)
    {
        if (CharacterPtr spCharacter = spUser->GetCurrentCharacter())
        {
            SendStatUpdateNtf(accountId, *spCharacter);
            SendHpMpNtf(accountId, spCharacter->GetCurHp(), spCharacter->GetCurMp());
        }
    }

    LOG_WRITE(LogLevel::Info, std::format("StageLoadCompleteRes sent. accountId={} stageId={} stageKey={} pos=({},{},{}) yaw={}",
        accountId, stageId, stageDataKey, myPosX, myPosY, myPosZ, myYaw));
}

void PacketSender::SendStageMoveRes(int64 accountId, EResultCode resultCode, const std::string& errorMsg, int32 targetStageDataKey)
{
    GamePacket::StageMoveRes res;
    res.set_result_code(static_cast<int32>(resultCode));
    res.set_error_msg(errorMsg);
    res.set_target_stage_data_key(targetStageDataKey);

    SendToUser(accountId, Common::GAME_PACKET_ID_STAGE_MOVE_RES, res);
}

void PacketSender::SendObjectVisibilityNtf(int64 accountId,
                                           const std::vector<GamePacket::CharacterSpawnInfo>& characterSpawns,
                                           const std::vector<int64>& despawnIds,
                                           const std::vector<GamePacket::MonsterSpawnInfo>& monsterSpawns,
                                           const std::vector<GamePacket::PropSpawnInfo>& propSpawns)
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

    for (const auto& spawn : propSpawns)
    {
        *ntf.add_prop_spawns() = spawn;
    }

    for (int64 id : despawnIds)
    {
        ntf.add_despawn_ids(id);
    }

    SendToUser(accountId, Common::GAME_PACKET_ID_OBJECT_VISIBILITY_NTF, ntf);

    LOG_WRITE(LogLevel::Info, std::format("ObjectVisibilityNtf sent. accountId={} characterSpawns={} monsterSpawns={} propSpawns={} despawns={}",
        accountId, characterSpawns.size(), monsterSpawns.size(), propSpawns.size(), despawnIds.size()));
}

void PacketSender::SendPropStateNtf(std::span<const int64> accountIds, int64 objectId, int32 state, int64 actorObjectId)
{
    GamePacket::PropStateNtf ntf;
    ntf.set_object_id(objectId);
    ntf.set_state(state);
    ntf.set_actor_object_id(actorObjectId);

    SendToUsers(accountIds, Common::GAME_PACKET_ID_PROP_STATE_NTF, ntf);

    LOG_WRITE(LogLevel::Info, std::format("PropStateNtf sent. recipients={} objectId={} state={} actorObjectId={}",
        accountIds.size(), objectId, state, actorObjectId));
}

void PacketSender::SendMovePosCorrectNtf(int64 accountId, float posX, float posY, float posZ, float yaw)
{
    GamePacket::MovePosCorrectNtf ntf;
    ntf.set_pos_x(posX);
    ntf.set_pos_y(posY);
    ntf.set_pos_z(posZ);
    ntf.set_yaw(yaw);

    SendToUser(accountId, Common::GAME_PACKET_ID_MOVE_POS_CORRECT_NTF, ntf);

    LOG_WRITE(LogLevel::Info, std::format("MovePosCorrectNtf sent. accountId={} pos=({},{},{}) yaw={}", accountId, posX, posY, posZ, yaw));
}

void PacketSender::SendSnapshotNtf(int64 accountId, const GamePacket::SnapshotNtf& ntf)
{
    // 고빈도(매 tick) 패킷이라 로그를 남기지 않는다. ntf 는 Stage 가 AOI 순회로 채워 전달한다.
    SendToUser(accountId, Common::GAME_PACKET_ID_SNAPSHOT_NTF, ntf);
}

void PacketSender::SendTimeSyncNtf(std::span<const int64> accountIds, uint32 serverTickSeq)
{
    // 저빈도 시각 앵커. 동일 payload 를 1회만 직렬화해 게이트웨이별로 묶어 broadcast 한다. 로그 생략.
    GamePacket::TimeSyncNtf ntf;
    ntf.set_server_tick_seq(serverTickSeq);
    SendToUsers(accountIds, Common::GAME_PACKET_ID_TIME_SYNC_NTF, ntf);
}

void PacketSender::SendStatUpdateNtf(int64 accountId, const Character& character)
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

    SendToUser(accountId, Common::GAME_PACKET_ID_STAT_UPDATE_NTF, ntf);

    LOG_WRITE(LogLevel::Info, std::format("StatUpdateNtf sent. accountId={} objectId={} count={}",
        accountId, character.GetObjectId(), ntf.entries_size()));
}

void PacketSender::SendHpMpNtf(int64 accountId, double curHp, double curMp)
{
    GamePacket::HpMpNtf ntf;
    // 본인에게만 보내는 Ntf. object_id 에는 소유 캐릭터의 objectId(=characterId)를 담는다.
    // accountId(연결 식별자)와는 별개 값이므로 User 의 현재 캐릭터에서 objectId 를 얻는다.
    // 캐릭터 미선택 등으로 못 찾으면 0(유효 objectId 없음 — 정상 흐름에선 발생하지 않음).
    UserPtr spUser;
    int64 objectId = 0;
    if (m_safeUsers.Find(accountId, spUser) && spUser)
    {
        if (CharacterPtr spCharacter = spUser->GetCurrentCharacter())
            objectId = spCharacter->GetObjectId();
    }

    ntf.set_object_id(objectId);
    ntf.set_cur_hp(curHp);
    ntf.set_cur_mp(curMp);

    SendToUser(accountId, Common::GAME_PACKET_ID_HP_MP_NTF, ntf);
}

void PacketSender::SendBuffNtf(std::span<const int64> accountIds, int64 objectId, int32 buffKey, int32 stackCount, int32 remainTimeMs)
{
    GamePacket::BuffNtf ntf;
    ntf.set_object_id(objectId);
    ntf.set_buff_key(buffKey);
    ntf.set_stack_count(stackCount);
    ntf.set_remain_time_ms(remainTimeMs);

    SendToUsers(accountIds, Common::GAME_PACKET_ID_BUFF_NTF, ntf);
}

void PacketSender::SendBuffRemoveNtf(std::span<const int64> accountIds, int64 objectId, int32 buffKey)
{
    GamePacket::BuffRemoveNtf ntf;
    ntf.set_object_id(objectId);
    ntf.set_buff_key(buffKey);

    SendToUsers(accountIds, Common::GAME_PACKET_ID_BUFF_REMOVE_NTF, ntf);
}

void PacketSender::SendSkillDamageNtf(std::span<const int64> accountIds, int64 targetObjectId, double damage, bool isDuplicate, double remainingHp,
                                      int64 attackerObjectId, int32 sourceSkillKey)
{
    GamePacket::SkillDamageNtf ntf;
    ntf.set_target_object_id(targetObjectId);
    ntf.set_damage(static_cast<float>(damage));
    ntf.set_is_duplicate(isDuplicate);
    ntf.set_remaining_hp(static_cast<float>(remainingHp));
    ntf.set_attacker_object_id(attackerObjectId);
    ntf.set_source_skill_key(sourceSkillKey);

    SendToUsers(accountIds, Common::GAME_PACKET_ID_SKILL_DAMAGE_NTF, ntf);
}

void PacketSender::SendAbilityCastNtf(std::span<const int64> accountIds, int64 casterObjectId, int32 skillKey, int64 targetObjectId,
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

    SendToUsers(accountIds, Common::GAME_PACKET_ID_ABILITY_CAST_NTF, ntf);
}

void PacketSender::SendSkillCastNtf(std::span<const int64> accountIds, int64 casterObjectId, int32 skillKey, int64 effectId,
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

    SendToUsers(accountIds, Common::GAME_PACKET_ID_SKILL_CAST_NTF, ntf);
}

void PacketSender::SendObjectDeathNtf(std::span<const int64> accountIds, int64 objectId, int64 killerObjectId)
{
    GamePacket::ObjectDeathNtf ntf;
    ntf.set_object_id(objectId);
    ntf.set_killer_object_id(killerObjectId);

    SendToUsers(accountIds, Common::GAME_PACKET_ID_OBJECT_DEATH_NTF, ntf);
}

void PacketSender::SendObjectReviveNtf(std::span<const int64> accountIds, int64 objectId,
                                       float posX, float posY, float posZ, float yaw, double hp, double mp)
{
    GamePacket::ObjectReviveNtf ntf;
    ntf.set_object_id(objectId);
    ntf.set_pos_x(posX);
    ntf.set_pos_y(posY);
    ntf.set_pos_z(posZ);
    ntf.set_yaw(yaw);
    ntf.set_hp(hp);
    ntf.set_mp(mp);

    SendToUsers(accountIds, Common::GAME_PACKET_ID_OBJECT_REVIVE_NTF, ntf);
}

void PacketSender::SendStageNoticeNtf(std::span<const int64> accountIds, const std::string& message, int32 durationMs)
{
    GamePacket::StageNoticeNtf ntf;
    ntf.set_message(message);
    ntf.set_duration_ms(durationMs);

    SendToUsers(accountIds, Common::GAME_PACKET_ID_STAGE_NOTICE_NTF, ntf);
}
