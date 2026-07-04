#pragma once

#include "pch.h"
#include "ServerBase.h"
#include "User.h"
#include "ThreadSafeUnorderedMap.h"
#include "ShardedThreadSafeUnorderedMap.h"

#include <span>
#include "Enum/GameEnum_Common.h"
#include "GameServerDefine.h" 

// 전방선언 (SendStatUpdateNtf 의 const Character& 파라미터. 완전타입은 PacketSender.cpp 에서 include.)
class Character;

#ifdef _DEBUG
// [개발 계측] 패킷 타입별 송신량 집계 + 주기(2s) 자동 덤프. 모든 S->C 패킷이 거치는 단일 지점
// (sendClientPacketViaGateway)에서 Record 를 호출한다. bytes 는 "클라 수신 기준"(header+payload)에
// 수신자 수를 곱한 값(게이트웨이가 sidecar 를 떼고 전달하므로). 구현/상태는 PacketSender.cpp.
namespace packetstats { void Record(int32 packetType, int32 bytesPerRecipient, int32 recipientCount); }
#endif

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
//   - m_safeUsers            : accountId → UserPtr (대상 유저 조회).
//   - m_safeGatewaySessions  : gatewayId → 세션 (유저가 접속한 게이트웨이 조회).
//
// SendToUser 템플릿이 공통 송신 배관이고, 모든 Send***Ntf 편의함수가 이를 거친다.
// GameServer 의 흐름별 응답 헬퍼(sendGameEnterNtf 등)도 SendToUser 를 직접 사용한다.
class PacketSender
{
public:
    PacketSender(serverbase::ServerBase& server,
                 ShardedThreadSafeUnorderedMap<int64, UserPtr>& safeUsers,
                 SharedThreadSafeUnorderedMap<int32, netlib::ISessionPtr>& safeGatewaySessions)
        : m_server(server)
        , m_safeUsers(safeUsers)
        , m_safeGatewaySessions(safeGatewaySessions)
    {
    }

    PacketSender(const PacketSender&)            = delete;
    PacketSender& operator=(const PacketSender&) = delete;

    // ── 공통 송신 ──────────────────────────────────────────────
    // message 를 직렬화해 클라용 패킷으로 만들고, 수신자 accountId 를 sidecar 로 붙여 해당 유저가
    // 접속한 게이트웨이로 전송한다. (게이트웨이가 sidecar 를 떼고 클라에 전달)
    // 모든 단일 대상 Send***Ntf 가 이 함수를 거친다. (정의는 헤더 하단 — 템플릿)
    template <typename TMessage>
    void SendToUser(int64 accountId, int32 packetType, const TMessage& message);

    // 같은 message 를 여러 유저에게 전송한다(브로드캐스트). payload 를 1회만 직렬화하고,
    // 대상 유저를 게이트웨이별로 묶어 게이트웨이당 패킷 1개(수신자 accountId 목록 sidecar)로 보낸다.
    // AOI 브로드캐스트(스킬/사망/스폰 등 동일 payload→다수)는 이 함수를 사용한다.
    // accountIds 는 std::span 으로 받아 호출측이 재사용 버퍼를 넘길 수 있게 한다(소유·복사 없음).
    template <typename TMessage>
    void SendToUsers(std::span<const int64> accountIds, int32 packetType, const TMessage& message);

    // ── 패킷별 편의 함수 ───────────────────────────────────────
    // Stage 로딩 완료 결과 + 캐릭터 스폰 확정 전송 (StageLoadCompleteRes).
    // 클라의 StageLoadCompleteReq에 대한 응답. 서버가 결정한 spawn 위치/회전을 포함한다.
    // 다른 주변 오브젝트 정보는 ObjectVisibilityNtf로 별도 전송.
    void SendStageLoadCompleteRes(int64 accountId, EResultCode resultCode, int64 stageId, int32 stageDataKey,
                                  float myPosX, float myPosY, float myPosZ, float myYaw,
                                  float worldMinX, float worldMinZ, float worldMaxX, float worldMaxZ);

    // Stage 이동 요청 결과 전송 (StageMoveRes). 성공 = 클라는 로딩 시작.
    void SendStageMoveRes(int64 accountId, EResultCode resultCode, const std::string& errorMsg, int32 targetStageDataKey);

    // 오브젝트 가시성 알림 전송 (ObjectVisibilityNtf). accountId에게 spawns/despawnIds 전송.
    void SendObjectVisibilityNtf(int64 accountId,
                                 const std::vector<GamePacket::CharacterSpawnInfo>& characterSpawns,
                                 const std::vector<int64>& despawnIds,
                                 const std::vector<GamePacket::MonsterSpawnInfo>& monsterSpawns = {},
                                 const std::vector<GamePacket::PropSpawnInfo>& propSpawns = {});

    // prop 상태 변경 알림 전송 (PropStateNtf). Stage 가 상태전이 시 prop 주변 AOI 유저들에게 broadcast.
    // actorObjectId: 상호작용을 유발한 액터(없으면 0, 예: 스크립트 SetPropState).
    void SendPropStateNtf(std::span<const int64> accountIds, int64 objectId, int32 state, int64 actorObjectId);

    // 위치 보정 알림 전송 (MovePosCorrectNtf). 서버가 클라/서버 위치 오차가 크다고 판단했을 때 unicast.
    void SendMovePosCorrectNtf(int64 accountId, float posX, float posY, float posZ, float yaw);

    // AOI 스냅샷 전송 (SnapshotNtf). Stage 가 매 tick 유저별로 AOI 내 보이는 오브젝트 상태를 모아 unicast.
    // 고빈도 패킷이라 로그를 남기지 않는다.
    void SendSnapshotNtf(int64 accountId, const GamePacket::SnapshotNtf& ntf);

    // NetClock 시각 동기 전송 (TimeSyncNtf). Stage 가 저빈도(2Hz)로 stage 내 전 유저에게 broadcast.
    // SnapshotNtf 와 독립적으로 클라 재생 시계를 앵커링한다. payload 극소 + 저빈도라 로그 생략.
    void SendTimeSyncNtf(std::span<const int64> accountIds, uint32 serverTickSeq);

    // 스탯 스냅샷 전송 (StatUpdateNtf). character 의 0 아닌 스탯만 담아 본인에게 unicast.
    void SendStatUpdateNtf(int64 accountId, const Character& character);

    // 현재 HP/MP 전송 (HpMpNtf). 본인에게 unicast. 대미지/회복 시점에도 사용.
    void SendHpMpNtf(int64 accountId, double curHp, double curMp);

    // 버프 뱃지 알림 전송 (BuffNtf / BuffRemoveNtf). UI 뱃지 데이터(키/스택/남은시간)만 담는다.
    // remainTimeMs: -1 이면 영구(클라에서 카운트다운 표시 안 함).
    void SendBuffNtf(std::span<const int64> accountIds, int64 objectId, int32 buffKey, int32 stackCount, int32 remainTimeMs);
    void SendBuffRemoveNtf(std::span<const int64> accountIds, int64 objectId, int32 buffKey);

    // 스킬 대미지 알림 전송 (SkillDamageNtf). Stage 가 대미지 적용 시점에 대상 주변 AOI 유저들에게 broadcast.
    // attackerObjectId/sourceSkillKey 는 클라 방향 피격표식·연출 분기용 (없으면 0).
    void SendSkillDamageNtf(std::span<const int64> accountIds, int64 targetObjectId, double damage, bool isDuplicate, double remainingHp,
                            int64 attackerObjectId = 0, int32 sourceSkillKey = 0);

    // 스킬 시전 알림 전송 (SkillCastNtf). Stage 가 시전자 주변 AOI 유저들에게 broadcast. 클라 비주얼 재현용.
    void SendSkillCastNtf(std::span<const int64> accountIds, int64 casterObjectId, int32 skillKey, int64 effectId,
                          float originX, float originY, float originZ, float dirX, float dirZ, uint32 seed,
                          float moveDistance);

    // 능력 시전 "시작" 알림 전송 (AbilityCastNtf). Stage 가 시전자 주변 AOI 유저들에게 broadcast.
    // 클라가 윈드업 모션 + 텔레그래프를 재생한다 (몬스터/NPC/엘리트 공용).
    void SendAbilityCastNtf(std::span<const int64> accountIds, int64 casterObjectId, int32 skillKey, int64 targetObjectId,
                            float originX, float originY, float originZ, float dirX, float dirZ, int32 windupMs);

    // 오브젝트 사망 알림 전송 (ObjectDeathNtf). Stage 가 사망한 대상 주변 AOI 유저들에게 broadcast. 클라 사망 연출용.
    void SendObjectDeathNtf(std::span<const int64> accountIds, int64 objectId, int64 killerObjectId);

    // 오브젝트 부활 알림 전송 (ObjectReviveNtf). Stage 가 부활한 대상 주변 AOI 유저들에게 broadcast. 클라 부활 연출/상태복원용.
    void SendObjectReviveNtf(std::span<const int64> accountIds, int64 objectId,
                             float posX, float posY, float posZ, float yaw, double hp, double mp);

    // 코스메틱 액션 알림 전송 (ActorActionNtf). Stage 가 점프/감정표현을 주변 AOI 에 relay. 연출 전용.
    void SendActorActionNtf(std::span<const int64> accountIds, int64 actorObjectId, int32 actionId, const std::string& param);

    // Stage 공지 배너 전송 (StageNoticeNtf). Stage 로직 스크립트의 Notice() 가 발생. 클라는 화면 배너 표시.
    void SendStageNoticeNtf(std::span<const int64> accountIds, const std::string& message, int32 durationMs);

private:
    // message 를 클라용 패킷 [Header(packetType)][payload] 의 버퍼에 직접 직렬화하고(중간 std::string 없음),
    // 수신자 accountId 목록을 sidecar 로 붙여 게이트웨이 세션으로 전송한다. accountId 수가 많아 패킷 최대크기(uint16)를
    // 넘으면 여러 패킷으로 분할한다. (SendToUser=1명, SendToUsers=N명이 공통으로 사용)
    template <typename TMessage>
    void sendClientPacketViaGateway(const netlib::ISessionPtr& spGatewaySession, int32 packetType,
                                    const TMessage& message, const int64* accountIds, int32 accountIdCount);

private:
    serverbase::ServerBase&                                   m_server;
    ShardedThreadSafeUnorderedMap<int64, UserPtr>&           m_safeUsers;
    SharedThreadSafeUnorderedMap<int32, netlib::ISessionPtr>& m_safeGatewaySessions;
};


// ─────────────────────────────────────────────────────────────
// template 구현
// ─────────────────────────────────────────────────────────────
template <typename TMessage>
void PacketSender::SendToUser(int64 accountId, int32 packetType, const TMessage& message)
{
    UserPtr spUser;
    if (!m_safeUsers.Find(accountId, spUser) || !spUser)
    {
        LOG_WRITE(LogLevel::Warn, std::format("user not found. accountId={} packetType={}", accountId, packetType));
        return;
    }

    netlib::ISessionPtr spGatewaySession;
    if (!m_safeGatewaySessions.Find(spUser->GetGatewayId(), spGatewaySession) || !spGatewaySession)
    {
        LOG_WRITE(LogLevel::Warn, std::format("gateway session not found. accountId={} gatewayId={} packetType={}", accountId, spUser->GetGatewayId(), packetType));
        return;
    }

    // [치트] 송신 패킷 로깅. message 가 타입으로 살아있어 이름·JSON 모두 한 줄로 가능.
    if (const EPacketLogMode logMode = packetlog::EffectiveMode(*spUser); logMode != EPacketLogMode::None)
        packetlog::LogPacket("S->C", accountId, static_cast<uint16>(packetType),
                             logMode == EPacketLogMode::Detail ? &message : nullptr);

    // accountId 1개를 sidecar 로 붙여 게이트웨이로 전송한다. (패킷 버퍼에 직접 직렬화)
    sendClientPacketViaGateway(spGatewaySession, packetType, message, &accountId, 1);
}

template <typename TMessage>
void PacketSender::SendToUsers(std::span<const int64> accountIds, int32 packetType, const TMessage& message)
{
    if (accountIds.empty())
        return;

    // 대상 유저를 게이트웨이별로 묶는다. (한 Stage 의 유저가 서로 다른 게이트웨이를 경유할 수 있음)
    // thread_local 로 재사용해 호출마다의 힙 할당을 제거한다(컨텐츠 스레드별 1개, 버킷 capacity 유지).
    thread_local std::unordered_map<int32, std::vector<int64>> idsByGateway;
    for (auto& [gatewayId, ids] : idsByGateway)
        ids.clear();   // 엔트리(게이트웨이 키)는 유지하고 내용만 비운다 → 재해시/재할당 없음

    for (int64 accountId : accountIds)
    {
        UserPtr spUser;
        if (!m_safeUsers.Find(accountId, spUser) || !spUser)
            continue;

        // [치트] 송신 패킷 로깅.
        if (const EPacketLogMode logMode = packetlog::EffectiveMode(*spUser); logMode != EPacketLogMode::None)
            packetlog::LogPacket("S->C", accountId, static_cast<uint16>(packetType),
                                 logMode == EPacketLogMode::Detail ? &message : nullptr);

        idsByGateway[spUser->GetGatewayId()].push_back(accountId);
    }

    // 게이트웨이당 패킷 1개로 전송한다.
    for (auto& [gatewayId, ids] : idsByGateway)
    {
        if (ids.empty())
            continue;   // 이번 호출에서 대상이 없는 게이트웨이(이전 호출에서 남은 빈 버킷)

        netlib::ISessionPtr spGatewaySession;
        if (!m_safeGatewaySessions.Find(gatewayId, spGatewaySession) || !spGatewaySession)
        {
            LOG_WRITE(LogLevel::Warn, std::format("gateway session not found. gatewayId={} packetType={}", gatewayId, packetType));
            continue;
        }

        sendClientPacketViaGateway(spGatewaySession, packetType, message, ids.data(), static_cast<int32>(ids.size()));
    }
}

template <typename TMessage>
void PacketSender::sendClientPacketViaGateway(const netlib::ISessionPtr& spGatewaySession, int32 packetType,
                                              const TMessage& message, const int64* accountIds, int32 accountIdCount)
{
    const int32 headerSize  = static_cast<int32>(sizeof(netlib::PacketHeader));
    const int32 sidecarHdr  = static_cast<int32>(sizeof(netlib::SidecarHeader));
    const int32 payloadSize = packet::ProtoSerializer::GetPayloadSize(message);

#ifdef _DEBUG
    // [개발 계측] 클라 수신 기준(header+payload) × 수신자 수. 타입별 송신량 집계.
    packetstats::Record(packetType, headerSize + payloadSize, accountIdCount);
#endif

    // payload 가 고정이므로 한 패킷에 담을 수 있는 accountId 개수를 미리 계산한다.
    const int32 maxSidecarBytes = 0xFFFF - headerSize - payloadSize - sidecarHdr;
    int32 maxIdsPerPacket = maxSidecarBytes / static_cast<int32>(sizeof(int64));
    if (maxIdsPerPacket < 1)
        maxIdsPerPacket = 1;   // payload 가 비정상적으로 큰 경우에도 루프 진행을 보장 (SetSidecar 에서 실패 로깅)

    for (int32 offset = 0; offset < accountIdCount; offset += maxIdsPerPacket)
    {
        const int32 count         = (maxIdsPerPacket < accountIdCount - offset) ? maxIdsPerPacket : (accountIdCount - offset);
        const int32 sidecarBytes  = count * static_cast<int32>(sizeof(int64));
        const int32 totalCapacity = headerSize + payloadSize + sidecarHdr + sidecarBytes;

        netlib::PacketPtr spPacket = m_server.GetIoContext().GetPacketPool().Alloc(totalCapacity);
        if (!spPacket)
        {
            LOG_WRITE(LogLevel::Error, std::format("packet pool alloc failed. packetType={} size={}", packetType, totalCapacity));
            return;
        }

        // sidecar 를 먼저 깐다. 이 시점 payload 가 0바이트라 SetSidecar 내부 memmove 가 스킵된다.
        // SetSidecar 가 header.size 를 읽으므로, 먼저 빈 payload 상태(size=headerSize)의 헤더를 세팅한다.
        spPacket->SetHeader(static_cast<uint16>(headerSize), static_cast<uint16>(packetType), netlib::PacketFlags::None);
        if (!spPacket->SetSidecar(accountIds + offset, sidecarBytes))
        {
            LOG_WRITE(LogLevel::Error, std::format("SetSidecar failed. packetType={} count={}", packetType, count));
            return;
        }

        // payload 를 sidecar 이후 위치(GetPayload())에 직접 직렬화하고, payload 크기만큼 header.size 를 확정한다.
        // (payload→sidecar 순서가 아니므로 memmove 가 없다.)
        if (payloadSize > 0 && !packet::ProtoSerializer::Serialize(message, spPacket->GetPayload(), payloadSize))
        {
            LOG_WRITE(LogLevel::Error, std::format("serialize failed. packetType={} payloadSize={}", packetType, payloadSize));
            return;
        }
        spPacket->FinalizePacketSize(payloadSize);

        spGatewaySession->Send(spPacket);
    }
}
