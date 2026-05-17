#pragma once

#include "pch.h"

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

    // ── 클라 패킷 큐 ────────────────────────────────────────────
    std::mutex                       m_packetQueueMutex;
    std::vector<netlib::PacketPtr>   m_packetQueue;
};

using UserPtr  = std::shared_ptr<User>;
using UserWPtr = std::weak_ptr<User>;
