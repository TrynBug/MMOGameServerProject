#pragma once

#include "pch.h"
#include "ServerBase.h"
#include "User.h"
#include "ThreadSafeUnorderedMap.h"
#include "Enum/GameEnum_Common.h"

// 전방선언 (SendStatUpdateNtf 의 const Character& 파라미터. 완전타입은 PacketSender.cpp 에서 include.)
class Character;

// ─────────────────────────────────────────────────────────────
// PacketSender
// ─────────────────────────────────────────────────────────────
//
// 게임서버 → 클라이언트로 나가는 아웃바운드 패킷(Ntf)을 만들어 게이트웨이 경유로 전송하는
// 단일 책임을 가지는 클래스. GameServer 가 멤버로 소유하며(GetPacketSender), Stage/컴포넌트는
// 이 객체를 통해 알림을 보낸다.
//
// 의존성(모두 참조로 보관, 소유 X — lifetime 은 GameServer 가 보장):
//   - serverbase::ServerBase : 패킷 직렬화(SerializePacket).
//   - m_safeUsers            : userId → UserPtr (대상 유저 조회).
//   - m_safeGatewaySessions  : gatewayId → 세션 (유저가 접속한 게이트웨이 조회).
//
// SendToUser 템플릿이 공통 송신 배관이고, 모든 Send***Ntf 편의함수가 이를 거친다.
// GameServer 의 흐름별 응답 헬퍼(sendGameEnterNtf 등)도 SendToUser 를 직접 사용한다.
class PacketSender
{
public:
    PacketSender(serverbase::ServerBase& server,
                 SharedThreadSafeUnorderedMap<int64, UserPtr>& safeUsers,
                 SharedThreadSafeUnorderedMap<int32, netlib::ISessionPtr>& safeGatewaySessions)
        : m_server(server)
        , m_safeUsers(safeUsers)
        , m_safeGatewaySessions(safeGatewaySessions)
    {
    }

    PacketSender(const PacketSender&)            = delete;
    PacketSender& operator=(const PacketSender&) = delete;

    // ── 공통 송신 ──────────────────────────────────────────────
    // message 를 직렬화해 GameToGatewayPacketNtf 로 래핑하여 해당 유저가 접속한 게이트웨이로 전송한다.
    // 모든 Send***Ntf 가 이 함수를 거친다. (정의는 헤더 하단 — 템플릿)
    template <typename TMessage>
    void SendToUser(int64 userId, int32 packetType, const TMessage& message);

    // ── 패킷별 편의 함수 ───────────────────────────────────────
    // Stage 로딩 완료 결과 + 캐릭터 스폰 확정 전송 (StageLoadCompleteRes).
    // 클라의 StageLoadCompleteReq에 대한 응답. 서버가 결정한 spawn 위치/회전을 포함한다.
    // 다른 주변 오브젝트 정보는 ObjectVisibilityNtf로 별도 전송.
    void SendStageLoadCompleteRes(int64 userId, EResultCode resultCode, int64 stageId, int32 stageDataKey,
                                  float myPosX, float myPosY, float myPosZ, float myYaw);

    // Stage 이동 요청 결과 전송 (StageMoveRes). 성공 = 클라는 로딩 시작.
    void SendStageMoveRes(int64 userId, EResultCode resultCode, const std::string& errorMsg, int32 targetStageDataKey);

    // 오브젝트 가시성 알림 전송 (ObjectVisibilityNtf). userId에게 spawns/despawnIds 전송.
    void SendObjectVisibilityNtf(int64 userId,
                                 const std::vector<GamePacket::CharacterSpawnInfo>& characterSpawns,
                                 const std::vector<int64>& despawnIds,
                                 const std::vector<GamePacket::MonsterSpawnInfo>& monsterSpawns = {});

    // 위치 보정 알림 전송 (MovePosCorrectNtf). 서버가 클라/서버 위치 오차가 크다고 판단했을 때 unicast.
    void SendMovePosCorrectNtf(int64 userId, float posX, float posY, float posZ, float yaw);

    // AOI 스냅샷 전송 (SnapshotNtf). Stage 가 매 tick 유저별로 AOI 내 보이는 오브젝트 상태를 모아 unicast.
    // 고빈도 패킷이라 로그를 남기지 않는다.
    void SendSnapshotNtf(int64 userId, const GamePacket::SnapshotNtf& ntf);

    // 스탯 스냅샷 전송 (StatUpdateNtf). character 의 0 아닌 스탯만 담아 본인에게 unicast.
    void SendStatUpdateNtf(int64 userId, const Character& character);

    // 현재 HP/MP 전송 (HpMpNtf). 본인에게 unicast. 대미지/회복 시점에도 사용.
    void SendHpMpNtf(int64 userId, double curHp, double curMp);

    // 버프 뱃지 알림 전송 (BuffNtf / BuffRemoveNtf). UI 뱃지 데이터(키/스택/남은시간)만 담는다.
    // remainTimeMs: -1 이면 영구(클라에서 카운트다운 표시 안 함).
    void SendBuffNtf(int64 userId, int64 objectId, int32 buffKey, int32 stackCount, int32 remainTimeMs);
    void SendBuffRemoveNtf(int64 userId, int64 objectId, int32 buffKey);

    // 스킬 대미지 알림 전송 (SkillDamageNtf). Stage 가 대미지 적용 시점에 대상 주변 AOI 유저들에게 broadcast.
    void SendSkillDamageNtf(int64 userId, int64 targetObjectId, double damage, bool isDuplicate, double remainingHp);

    // 스킬 시전 알림 전송 (SkillCastNtf). Stage 가 시전자 주변 AOI 유저들에게 broadcast. 클라 비주얼 재현용.
    void SendSkillCastNtf(int64 userId, int64 casterObjectId, int32 skillKey, int64 effectId,
                          float originX, float originY, float originZ, float dirX, float dirZ, uint32 seed,
                          float moveDistance);

    // 오브젝트 사망 알림 전송 (ObjectDeathNtf). Stage 가 사망한 대상 주변 AOI 유저들에게 broadcast. 클라 사망 연출용.
    void SendObjectDeathNtf(int64 userId, int64 objectId, int64 killerObjectId);

private:
    serverbase::ServerBase&                                   m_server;
    SharedThreadSafeUnorderedMap<int64, UserPtr>&             m_safeUsers;
    SharedThreadSafeUnorderedMap<int32, netlib::ISessionPtr>& m_safeGatewaySessions;
};


// ─────────────────────────────────────────────────────────────
// template 구현
// ─────────────────────────────────────────────────────────────
template <typename TMessage>
void PacketSender::SendToUser(int64 userId, int32 packetType, const TMessage& message)
{
    UserPtr spUser;
    if (!m_safeUsers.Find(userId, spUser) || !spUser)
    {
        LOG_WRITE(LogLevel::Warn, std::format("user not found. userId={} packetType={}", userId, packetType));
        return;
    }

    netlib::ISessionPtr spGatewaySession;
    if (!m_safeGatewaySessions.Find(spUser->GetGatewayId(), spGatewaySession) || !spGatewaySession)
    {
        LOG_WRITE(LogLevel::Warn, std::format("gateway session not found. userId={} gatewayId={} packetType={}", userId, spUser->GetGatewayId(), packetType));
        return;
    }

    // 내부 패킷 바디(클라용)를 먼저 직렬화한다.
    std::string payload;
    if (!message.SerializeToString(&payload))
    {
        LOG_WRITE(LogLevel::Error, std::format("failed to serialize payload. userId={} packetType={}", userId, packetType));
        return;
    }

    // GameToGatewayPacketNtf로 감싸서 게이트웨이로 전송.
    ServerPacket::GameToGatewayPacketNtf ntf;
    ntf.set_user_id(userId);
    ntf.set_packet_type(packetType);
    ntf.set_payload(std::move(payload));

    auto spPacket = m_server.SerializePacket(Common::SERVER_PACKET_ID_GAME_TO_GATEWAY_PACKET_NTF, ntf);
    if (!spPacket)
    {
        LOG_WRITE(LogLevel::Error, std::format("failed to serialize GameToGatewayPacketNtf. userId={} packetType={}", userId, packetType));
        return;
    }

    spGatewaySession->Send(spPacket);
}
