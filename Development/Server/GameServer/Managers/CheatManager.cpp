#include "pch.h"
#include "Managers/CheatManager.h"
#include "Stages/Stage.h"
#include "User.h"

CheatManager::CheatManager()
{
    // 새 서버치트는 여기에 한 줄 등록 + 아래에 핸들러 멤버 함수 작성.
    m_table = {
        { "ping", &CheatManager::cheatPing },
        { "god",  &CheatManager::cheatGod  },
        { "packet",       &CheatManager::cheatPacket },
        { "packetdetail", &CheatManager::cheatPacketDetail },
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
