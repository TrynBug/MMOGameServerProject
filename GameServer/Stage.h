#pragma once

#include "pch.h"
#include "User.h"

#include <variant>

// Stage 종류
enum class StageType : uint8
{
    Unknown      = 0,
    OpenField    = 1,   // 오픈필드 (게임서버당 1개, 전용 컨텐츠 스레드)
    PublicDungeon = 2,  // 공용던전 (시간 기반 입장)
    UserDungeon  = 3,   // 유저던전 (1명이 생성)
};


// ─────────────────────────────────────────────────────────────
// Stage 시스템 메시지
// 외부 스레드(IOCP Worker)에서 Stage에 비동기로 알림을 전달할 때 사용.
// std::variant로 표현하여 핸들러 분기를 깔끔하게 처리한다.
// ─────────────────────────────────────────────────────────────

// 유저 입장 (게이트웨이로부터 GatewayUserEnterNtf 수신 시)
struct StageMsg_UserEnter
{
    UserPtr spUser;
};

// 유저 퇴장 (게이트웨이로부터 GatewayUserDisconnectNtf 수신 시,
// 또는 다른 Stage로 이동할 때)
struct StageMsg_UserLeave
{
    int64 userId = 0;
};

// 모든 메시지 타입의 variant
using StageMessage = std::variant<
    StageMsg_UserEnter,
    StageMsg_UserLeave
>;


// ─────────────────────────────────────────────────────────────
// Stage 베이스 클래스
// ─────────────────────────────────────────────────────────────
//
// 캐릭터가 돌아다닐 수 있는 1개의 필드를 나타낸다.
// 단일 컨텐츠 스레드에서만 업데이트되므로 내부 로직은 단일 스레드로 작성한다.
// 외부에서 Stage에 영향을 주려면 EnqueueMessage()로 시스템 메시지를 보낸다.
// 자세한 설계는 서버구조개요.md의 'Stage 클래스' 절 참조.
//
// 향후 단계에서 추가될 것:
//   - 섹터 분할 (AOI)
//   - NavMesh 연결
//   - 몬스터 / NPC
class Stage : public serverbase::Contents
{
public:
    Stage(int64 stageId, StageType stageType);
    ~Stage() override = default;

    Stage(const Stage&) = delete;
    Stage& operator=(const Stage&) = delete;

public:
    int64     GetStageId()   const { return m_stageId; }
    StageType GetStageType() const { return m_stageType; }

    // 외부 스레드에서 시스템 메시지를 push (thread-safe).
    // 다음 OnUpdate에서 처리된다.
    void      EnqueueMessage(StageMessage msg);

protected:
    // serverbase::Contents 훅
    void OnStart()              override;
    void OnUpdate(int64 deltaMs) override;
    void OnStop()               override;

    // Stage 파생 클래스의 매 tick 로직.
    // OnUpdate 안에서 시스템 메시지 처리 이후에 호출된다.
    virtual void OnStageUpdate(int64 deltaMs) {}

    // ── 시스템 메시지 처리 hooks (파생 클래스가 override 가능) ──
    // 기본 동작: 유저 추가/제거 및 로그 출력.
    virtual void OnUserEnter(const UserPtr& spUser);
    virtual void OnUserLeave(int64 userId);

    // 유저가 클라이언트로부터 보낸 패킷 처리 hook (파생 클래스가 override 가능).
    // 기본 동작: 로그만 출력. 향후 단계에서 실제 디스패쳐 호출 등을 추가한다.
    virtual void OnUserPacket(const UserPtr& spUser, const netlib::PacketPtr& spPacket);

    // ── 유저 컨테이너 접근 ──
    const std::unordered_map<int64, UserPtr>& GetUsers() const { return m_users; }

private:
    // 시스템 메시지 큐 처리
    void processSystemMessages();

    // 각 유저의 클라 패킷 큐 drain 및 처리
    void processUserPackets();

private:
    int64     m_stageId   = 0;
    StageType m_stageType = StageType::Unknown;

    // 5초 주기 heartbeat 로그용 누적 시간
    int64     m_heartbeatAccumMs = 0;

    // ── 시스템 메시지 큐 (외부 스레드 push, 컨텐츠 스레드 drain) ──
    // swap-and-drain 패턴: 외부에서는 m_pendingMessages에 추가, 컨텐츠 스레드는
    // 락 잠깐 잡고 swap한 뒤 락 풀고 순차 처리. 락 충돌 최소화.
    std::mutex                m_pendingMessagesMutex;
    std::vector<StageMessage> m_pendingMessages;

    // ── 유저 컨테이너 (컨텐츠 스레드 전용 접근) ──
    // Stage가 유저를 소유 (shared_ptr).
    std::unordered_map<int64, UserPtr> m_users;
};

using StagePtr  = std::shared_ptr<Stage>;
using StageWPtr = std::weak_ptr<Stage>;
