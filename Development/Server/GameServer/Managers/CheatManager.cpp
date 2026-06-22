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
        { "savechar",     &CheatManager::cheatSaveChar },
#ifdef _DEBUG
        { "dbgstat",      &CheatManager::cheatDbgStat },
        { "dbgmon",       &CheatManager::cheatDbgMon  },
#endif
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
    req.set_account_id(spUser->GetAccountId());
    req.set_recv_delay_ms(recvMs);
    req.set_send_delay_ms(sendMs);

    GameServer& server = GameServer::Instance();
    auto spPacket = server.SerializePacket(Common::SERVER_PACKET_ID_SET_CLIENT_LATENCY_REQ, req);
    if (!spPacket || !server.SendToGateway(spUser->GetGatewayId(), spPacket))
        return { false, "failed to send latency request to gateway" };

    return { true, std::format("net delay set: recv={}ms send={}ms", recvMs, sendMs) };
}

// savechar: Stage에서 코루틴을 띄워 캐릭터 현재 상태를 DB에 저장하고, 후속작업이 같은 Stage 스레드에서
// 재개되는지 + AsyncPin 동작을 검증(개발용).
// 핸들러는 요청 유저의 Stage 컨텐츠 스레드에서 호출되므로, 여기서 시작한 코루틴은 그 Stage 스레드에서 시작된다.
// 결과는 서버 로그의 [savechar] 줄로 확인한다(코루틴은 fire-and-forget).
CheatResult CheatManager::cheatSaveChar(Stage& stage, const UserPtr& spUser,
                                        const std::vector<std::string>& /*args*/)
{
    if (!spUser)
        return { false, "no user context" };

    CharacterPtr spChar = spUser->GetCurrentCharacter();
    if (!spChar)
        return { false, "no character (select a character first)" };

    GameServer::Instance().SaveCharacterFromStage(&stage, spChar);
    return { true, "savechar launched. check server logs for [savechar] lines." };
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

#ifdef _DEBUG
// dbgstat <objectId>: 디버그 UI 의 스탯 보기 구독을 설정한다.
// 인자가 없거나 0 이면 해제. objectId 가 이 Stage 에 없으면 실패.
// 구독 상태는 호출 유저에 보관되고, Stage::sendDebugSubscriptions 가 주기적으로 DebugStatNtf 를 보낸다.
CheatResult CheatManager::cheatDbgStat(Stage& stage, const UserPtr& spUser, const std::vector<std::string>& args)
{
    if (!spUser)
        return { false, "no user context" };

    if (args.empty())
    {
        spUser->SetDebugStatTarget(0);
        return { true, "dbgstat off" };
    }

    const int64 objectId = std::atoll(args[0].c_str());
    if (objectId == 0)
    {
        spUser->SetDebugStatTarget(0);
        return { true, "dbgstat off" };
    }
    if (stage.FindObject(objectId) == nullptr)
        return { false, std::format("object not found in this stage: {}", objectId) };

    spUser->SetDebugStatTarget(objectId);
    return { true, std::format("dbgstat -> {}", objectId) };
}

// dbgmon [on|off]: 내 주변 섹터 몬스터 위치 구독 토글. 인자 없으면 현재값 토글.
CheatResult CheatManager::cheatDbgMon(Stage& /*stage*/, const UserPtr& spUser, const std::vector<std::string>& args)
{
    if (!spUser)
        return { false, "no user context" };

    bool on;
    if (args.empty())
        on = !spUser->IsDebugMonsterPos();
    else
        on = (args[0] == "on" || args[0] == "1" || args[0] == "true");

    spUser->SetDebugMonsterPos(on);
    return { true, std::string("dbgmon ") + (on ? "on" : "off") };
}
#endif // _DEBUG
