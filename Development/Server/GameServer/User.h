#pragma once

#include "pch.h"

#include "Enum/GameEnum_Stage.h"   // EStagePositionType

// Character와의 순환 의존을 피하기 위한 forward declaration.
class Character;
using CharacterPtr  = std::shared_ptr<Character>;
using CharacterWPtr = std::weak_ptr<Character>;

// 유저의 Stage 소속 상태.
// - None:    어느 Stage에도 캐릭터가 스폰되지 않음 (SystemStage 포함, 캐릭터 선택 전)
// - Moving:  Stage 이동 중. 캐릭터가 old Stage에서 분리되어 m_spPendingCharacter에 보관됨.
//            클라가 StageLoadCompleteReq를 보내면 target Stage가 스폰하고 InStage로 전환.
// - InStage: 캐릭터가 Stage에 스폰 완료된 상태.
enum class EUserStageState
{
    None,
    Moving,
    InStage,
};

// 유저(클라이언트) 1명을 나타내는 클래스
// 서버구조개요.md의 '유저 클래스' 절 참조.
//
// - 1개 Stage 객체에 소유된다 (Stage가 shared_ptr로 보유).
// - 단일 컨텐츠 스레드(소유 Stage의 스레드)에서만 업데이트되므로 내부는 single-thread로 작성.
// - 외부(IOCP Worker)에서 접근할 때는 GameServer의 글로벌 유저 맵을 통해 접근.
//
// 클라 패킷 큐:
//   IOCP Worker가 게이트웨이로부터 클라 패킷을 받으면 EnqueuePacket으로 push.
//   컨텐츠 스레드가 Stage Update 시 DrainPackets로 batch로 꺼내 처리.
//   swap-and-drain 패턴 (락 잠깐만 잡고 swap).
class User
{
public:
    User(int64 userId, int32 gatewayId, const std::string& clientIp);
    ~User() = default;

    User(const User&) = delete;
    User& operator=(const User&) = delete;

public:
    int64              GetUserId()    const { return m_userId; }
    int32              GetGatewayId() const { return m_gatewayId; }
    const std::string& GetClientIp()  const { return m_clientIp; }

    int64              GetCurrentStageId() const { return m_currentStageId; }
    void               SetCurrentStageId(int64 stageId) { m_currentStageId = stageId; }

    // 현재 선택된 캐릭터 (Stage가 강한 소유자, User는 weak_ptr로만 참조).
    // 캐릭터 선택 전이거나 Stage 입장 전이면 expired.
    CharacterPtr  GetCurrentCharacter() const { return m_wpCurrentCharacter.lock(); }
    CharacterWPtr GetCurrentCharacterWeak() const { return m_wpCurrentCharacter; }
    void          SetCurrentCharacter(const CharacterWPtr& wpCharacter) { m_wpCurrentCharacter = wpCharacter; }
    bool          HasSelectedCharacter() const { return !m_wpCurrentCharacter.expired(); }

    // ── Stage 이동/입장 상태 ─────────────────────────────────────
    // 상태는 서로 다른 스레드(코루틴 / old·target Stage 컨텐츠 스레드)에서 읽고 쓰므로 atomic.
    // pending 캐릭터/위치타입은 "쓰기 → EnqueueMessage → 읽기" 순서가 큐 mutex로 보장되므로 일반 멤버.
    EUserStageState GetStageState() const               { return m_stageState.load(std::memory_order_acquire); }
    void            SetStageState(EUserStageState s)    { m_stageState.store(s, std::memory_order_release); }

    // Stage 이동 동안 캐릭터를 임시 보관 (이 동안은 User가 유일한 강한 소유자).
    // positionType: 도착 위치 (StageStartPosition 조회용). None이면 캐릭터의 현재 좌표 사용 (DB 복귀).
    void SetPendingCharacter(const CharacterPtr& spCharacter, EStagePositionType positionType)
    {
        m_spPendingCharacter  = spCharacter;
        m_pendingPositionType = positionType;
    }
    CharacterPtr       GetPendingCharacter() const     { return m_spPendingCharacter; }
    EStagePositionType GetPendingPositionType() const  { return m_pendingPositionType; }
    void               ClearPendingCharacter()
    {
        m_spPendingCharacter.reset();
        m_pendingPositionType = EStagePositionType::None;
    }

    // ── 클라 패킷 큐 ────────────────────────────────────────────
    // IOCP Worker가 push (thread-safe)
    void EnqueuePacket(netlib::PacketPtr spPacket);

    // 컨텐츠 스레드가 한 번에 모든 패킷을 꺼낸다 (thread-safe, swap 방식)
    // outPackets에 채워 넣음. 호출 후 내부 큐는 비어있음.
    void DrainPackets(std::vector<netlib::PacketPtr>& outPackets);

private:
    int64       m_userId         = 0;
    int32       m_gatewayId      = 0;
    std::string m_clientIp;

    // 현재 소속 Stage ID. 입장 시 설정되고, Stage 이동 시 갱신된다.
    int64       m_currentStageId = 0;

    // 현재 선택된 캐릭터 (weak_ptr).
    // 라이프타임: Stage가 강한 소유자. User는 weak로만 참조.
    CharacterWPtr m_wpCurrentCharacter;

    // ── Stage 이동/입장 상태 ─────────────────────────────────────
    std::atomic<EUserStageState> m_stageState{ EUserStageState::None };

    // Stage 이동 동안의 캐릭터 임시 보관 (Moving 상태에서만 유효).
    CharacterPtr       m_spPendingCharacter;
    EStagePositionType m_pendingPositionType = EStagePositionType::None;

    // ── 클라 패킷 큐 ────────────────────────────────────────────
    std::mutex                       m_packetQueueMutex;
    std::vector<netlib::PacketPtr>   m_packetQueue;
};

using UserPtr  = std::shared_ptr<User>;
using UserWPtr = std::weak_ptr<User>;
