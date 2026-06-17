#include "pch.h"
#include "Managers/CheatManager.h"
#include "Stages/Stage.h"
#include "User.h"
#include "GameServer.h"

#include <cstdlib>

CheatManager::CheatManager()
{
    // 새 서버치트는 여기에 한 줄 등록 + 아래에 핸들러 멤버 함수 작성.
    m_table = {
        { "ping", &CheatManager::cheatPing },
        { "god",  &CheatManager::cheatGod  },
        { "packet",       &CheatManager::cheatPacket },
        { "packetdetail", &CheatManager::cheatPacketDetail },
        { "netdelay",     &CheatManager::cheatNetDelay },
    };
}

CheatResult CheatManager::Execute(Stage& stage, const UserPtr& spUser,
                                    const std::string& name, const std::vector<std::string>& args)
{
    auto iter = m_table.find(name);
    if (iter == m_table.end())
        return { false, "unknown cheat: " + name };

    return (this->*(iter->second))(stage, spUser, args);
}

// ── 개별 치트 ─────────────────────────────────────────────────

// ping: 왕복 동작 확인용. 인자를 그대로 되돌려준다 (상태 없음).
CheatResult CheatManager::cheatPing(Stage& /*stage*/, const UserPtr& /*spUser*/,
                                      const std::vector<std::string>& args)
{
    std::string joined;
    for (size_t i = 0; i < args.size(); ++i)
    {
        if (i != 0) joined += ' ';
        joined += args[i];
    }
    return { true, joined.empty() ? "pong" : ("pong " + joined) };
}

// god [on|off]: 무적 플래그 토글 (상태 보관 예시). 인자 없으면 현재값을 토글한다.
// 저장된 플래그는 다른 시스템이 GetCheatManager().IsGodMode() 로 조회한다.
CheatResult CheatManager::cheatGod(Stage& /*stage*/, const UserPtr& /*spUser*/,
                                     const std::vector<std::string>& args)
{
    bool on;
    if (args.empty())
        on = !m_godMode.load(std::memory_order_acquire);   // 토글
    else
        on = (args[0] == "on" || args[0] == "1" || args[0] == "true");

    m_godMode.store(on, std::memory_order_release);
    return { true, std::string("godmode ") + (on ? "on" : "off") };
}

// packet [all]: 패킷 "이름" 로깅 토글. all 이면 글로벌, 아니면 호출 유저.
CheatResult CheatManager::cheatPacket(Stage& /*stage*/, const UserPtr& spUser,
                                        const std::vector<std::string>& args)
{
    const bool all = (!args.empty() && args[0] == "all");
    return togglePacketLog(spUser, all, EPacketLogMode::Name, "packet");
}

// packetdetail [all]: 패킷 "이름 + 내용(JSON)" 로깅 토글. all 이면 글로벌, 아니면 호출 유저.
CheatResult CheatManager::cheatPacketDetail(Stage& /*stage*/, const UserPtr& spUser,
                                              const std::vector<std::string>& args)
{
    const bool all = (!args.empty() && args[0] == "all");
    return togglePacketLog(spUser, all, EPacketLogMode::Detail, "packetdetail");
}

// netdelay <recvMs> [sendMs]: 이 클라 연결에 인위적 네트워크 지연을 설정한다(개발용).
// 게임서버가 SetClientLatencyReq 를 해당 유저의 게이트웨이로 보내고, 게이트웨이가 클라 Session 에 적용한다.
// sendMs 생략 시 recvMs 와 동일. 0 0 이면 지연 해제.
CheatResult CheatManager::cheatNetDelay(Stage& /*stage*/, const UserPtr& spUser,
                                          const std::vector<std::string>& args)
{
    if (!spUser)
        return { false, "no user context" };
    if (args.empty())
        return { false, "usage: netdelay <recvMs> [sendMs]" };

    const int32 recvMs = std::atoi(args[0].c_str());
    const int32 sendMs = (args.size() >= 2) ? std::atoi(args[1].c_str()) : recvMs;
    if (recvMs < 0 || sendMs < 0)
        return { false, "delay must be >= 0" };

    ServerPacket::SetClientLatencyReq req;
    req.set_user_id(spUser->GetUserId());
    req.set_recv_delay_ms(recvMs);
    req.set_send_delay_ms(sendMs);

    GameServer& server = GameServer::Instance();
    auto spPacket = server.SerializePacket(Common::SERVER_PACKET_ID_SET_CLIENT_LATENCY_REQ, req);
    if (!spPacket || !server.SendToGateway(spUser->GetGatewayId(), spPacket))
        return { false, "failed to send latency request to gateway" };

    return { true, std::format("net delay set: recv={}ms send={}ms", recvMs, sendMs) };
}

// 공통 토글: 현재 모드가 level 이면 끄고(None), 아니면 level 로 켠다.
CheatResult CheatManager::togglePacketLog(const UserPtr& spUser, bool all, EPacketLogMode level, const char* label)
{
    if (all)
    {
        const EPacketLogMode next = (m_globalPacketLogMode == level) ? EPacketLogMode::None : level;
        m_globalPacketLogMode = next;
        return { true, std::format("{} all: {}", label, (next == EPacketLogMode::None) ? "off" : "on") };
    }

    if (!spUser)
        return { false, "no user context" };

    const EPacketLogMode cur  = spUser->GetCheatPacketLogMode();
    const EPacketLogMode next = (cur == level) ? EPacketLogMode::None : level;
    spUser->SetCheatPacketLogMode(next);
    return { true, std::format("{}: {} (this client)", label, (next == EPacketLogMode::None) ? "off" : "on") };
}
