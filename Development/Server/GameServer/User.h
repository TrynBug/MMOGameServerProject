#pragma once

#include "pch.h"

#include "Enum/GameEnum_Stage.h" 
#include "GameServerDefine.h"

// Character와의 순환 의존을 피하기 위한 forward declaration.
class Character;
using CharacterPtr  = std::shared_ptr<Character>;
using CharacterWPtr = std::weak_ptr<Character>;

// 유저의 Stage 소속 상태.
// - None:    어느 Stage에도 캐릭터가 스폰되지 않음 (SystemStage 포함, 캐릭터 선택 전)
// - Moving:  Stage 이동 중. 캐릭터는 old Stage 컨테이너에서 빠졌지만 User가 계속 소유(m_spCharacter)하여
//            살아있다. 클라가 StageLoadCompleteReq를 보내면 target Stage가 스폰하고 InStage로 전환.
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
// 캐릭터 소유: User가 자신의 Character를 강한 참조(shared_ptr)로 소유한다.
//   캐릭터는 선택 시점에 생성되어 User가 보관하고, 로그아웃/끊김으로 User가 파괴될 때 함께 파괴된다.
//   Stage는 스폰되어 있는 동안만 같은 캐릭터를 컨테이너에 함께 보관(빠른 조회/AOI용)한다.
//   Stage 이동 시 캐릭터는 old Stage에서 빠지지만 User가 계속 소유하므로 수명이 끊기지 않는다.
//   Character → User 역참조는 weak_ptr (순환참조 방지 + 세션 수명은 User가 결정).
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

    // 현재 캐릭터 (User가 강한 소유자). 선택 전이면 nullptr.
    // 선택 시점에 설정되고, Stage 이동에도 유지되며, 로그아웃/끊김 시 User 파괴와 함께 사라진다.
    CharacterPtr GetCurrentCharacter() const { return m_spCharacter; }
    void         SetCurrentCharacter(const CharacterPtr& spCharacter) { m_spCharacter = spCharacter; }
    bool         HasSelectedCharacter() const { return m_spCharacter != nullptr; }

    // ── Stage 이동/입장 상태 ─────────────────────────────────────
    // 상태는 서로 다른 스레드(코루틴 / old·target Stage 컨텐츠 스레드)에서 읽고 쓰므로 atomic.
    // 도착 위치타입은 "쓰기 → EnqueueMessage → 읽기" 순서가 큐 mutex로 보장되므로 일반 멤버.
    EUserStageState GetStageState() const               { return m_stageState.load(std::memory_order_acquire); }
    void            SetStageState(EUserStageState s)    { m_stageState.store(s, std::memory_order_release); }

    // 이동/입장 시 도착 위치 타입 (StageStartPosition 조회용). None이면 캐릭터 현재 좌표 사용 (DB 복귀).
    // 이동 요청 시점에 정해져 스폰 시점(spawnPendingCharacter)에 소비되는 전환 데이터.
    EStagePositionType GetPendingPositionType() const          { return m_pendingPositionType; }
    void               SetPendingPositionType(EStagePositionType positionType) { m_pendingPositionType = positionType; }

    // ── [치트] 패킷 로깅 모드 (per-client) ────────────────────────
    // packet/packetdetail 치트가 값을 설정한다. 패킷 송신, 패킷 수신 수신할 때마다 이 값을 읽는다.
    EPacketLogMode     GetCheatPacketLogMode() const             { return m_cheatPacketLogMode; }
    void               SetCheatPacketLogMode(EPacketLogMode mode) { m_cheatPacketLogMode = mode; }

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

    // 현재 캐릭터 (강한 소유). User가 Character의 주인이다. 선택 전이면 nullptr.
    CharacterPtr m_spCharacter;

    // ── Stage 이동/입장 상태 ─────────────────────────────────────
    std::atomic<EUserStageState> m_stageState{ EUserStageState::None };

    // 이동/입장 시 도착 위치 타입 (전환 데이터, 수명 무관).
    EStagePositionType m_pendingPositionType = EStagePositionType::None;

    // [치트] per-client 패킷 로깅 모드. 기본 None.
    EPacketLogMode m_cheatPacketLogMode = EPacketLogMode::None;

    // ── 클라 패킷 큐 ────────────────────────────────────────────
    std::mutex                       m_packetQueueMutex;
    std::vector<netlib::PacketPtr>   m_packetQueue;
};

using UserPtr  = std::shared_ptr<User>;
using UserWPtr = std::weak_ptr<User>;
