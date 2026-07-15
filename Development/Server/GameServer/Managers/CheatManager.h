#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "GameServerDefine.h"   // EPacketLogMode

// 치트 처리 + 치트 상태 보관 매니저 (개발용).
// GameServer 가 멤버로 1개 소유한다 (다른 Manager 들과 동일한 패턴).
//
// - Execute: CheatReq 의 name 으로 핸들러를 찾아 실행하고 결과를 돌려준다.
//   핸들러는 요청 유저가 속한 Stage 의 컨텐츠 스레드에서 호출된다.
//   핸들러가 "멤버 함수" 라 매니저 상태(플래그 등)를 읽고 쓸 수 있다.
// - 상태(플래그): 여러 Stage 의 컨텐츠 스레드에서 접근될 수 있으므로 thread-safe 하게 둔다 (atomic).
//
// 컴파일/게이팅:
//   매니저 자체는 항상 컴파일된다. 그래야 다른 시스템이 #ifdef 없이
//   GetCheatManager().IsGodMode() 처럼 플래그를 조회할 수 있다.
//   치트 "명령 수신" 경로(Stage 의 CHEAT_REQ 핸들러 등록)만 _DEBUG 로 막혀 있어,
//   라이브 빌드에서는 명령이 들어오지 않아 플래그가 기본값에서 바뀌지 않는다.

class Stage;
class User;
using UserPtr = std::shared_ptr<User>;

// 치트 1건 실행 결과. Stage::handleCheatReq 가 CheatRes 로 변환한다.
struct CheatResult
{
    bool        success = false;
    std::string message;
};


class CheatManager
{
public:
    CheatManager();

    CheatManager(const CheatManager&)            = delete;
    CheatManager& operator=(const CheatManager&) = delete;

    // name 으로 치트를 찾아 실행한다. 없으면 success=false, message="unknown cheat: ...".
    // stage/spUser 는 호출 컨텍스트(해당 Stage 의 컨텐츠 스레드).
    CheatResult Execute(Stage& stage, const UserPtr& spUser,
                          const std::string& name, const std::vector<std::string>& args);

    // ── 치트 상태 (예시) ──────────────────────────────────────────
    // 치트로 토글되고, 다른 시스템이 조회한다 (예: 전투에서 무적 처리).
    bool IsGodMode() const { return m_godMode.load(std::memory_order_acquire); }

    // [치트] 글로벌 패킷 로깅 모드 (packet all / packetdetail all). 모든 유저에 적용.
    EPacketLogMode GetGlobalPacketLogMode() const { return m_globalPacketLogMode; }

private:
    // 핸들러 시그니처: 매니저 자신(상태 접근) + 컨텍스트(Stage/User) + 인자 -> 결과.
    using Handler = CheatResult(CheatManager::*)(Stage& stage, const UserPtr& spUser,
                                                    const std::vector<std::string>& args);

    // ── 개별 치트 ─────────────────────────────────────────────
    // 새 서버치트 = 핸들러 멤버 함수 작성 + 생성자의 m_table 에 한 줄 등록.
    CheatResult cheatPing(Stage& stage, const UserPtr& spUser, const std::vector<std::string>& args);
    CheatResult cheatGod (Stage& stage, const UserPtr& spUser, const std::vector<std::string>& args);

    // packet [all] / packetdetail [all]: 패킷 로깅 토글.
    CheatResult cheatPacket      (Stage& stage, const UserPtr& spUser, const std::vector<std::string>& args);
    CheatResult cheatPacketDetail(Stage& stage, const UserPtr& spUser, const std::vector<std::string>& args);

    // netdelay <recvMs> [sendMs]: 이 클라 연결에 인위적 네트워크 지연 설정(개발용). 게이트웨이로 전파.
    CheatResult cheatNetDelay    (Stage& stage, const UserPtr& spUser, const std::vector<std::string>& args);

    // savechar: [개발] Stage에서 코루틴을 띄워 캐릭터 현재 상태를 DB에 저장하고, 후속작업이 같은 Stage
    // 스레드에서 재개되는지 + AsyncPin 카운터 동작을 로그([savechar])로 검증한다. 인자 없음.
    CheatResult cheatSaveChar    (Stage& stage, const UserPtr& spUser, const std::vector<std::string>& args);

    // channel [no]: 인자 없으면 현재 Stage 의 채널 현황(내 채널 + 채널별 인원)을 보고하고,
    // 번호를 입력하면 그 채널로 이동한다.
    CheatResult cheatChannel     (Stage& stage, const UserPtr& spUser, const std::vector<std::string>& args);

    // [디버그 UI] 구독 토글. 상태는 호출 유저(User)에 보관되고, Stage 디버그 tick 이 읽어 push 한다.
    // dbgstat <objectId>: 선택 오브젝트 전체 스탯 구독 (인자 없거나 0 이면 해제).
    CheatResult cheatDbgStat     (Stage& stage, const UserPtr& spUser, const std::vector<std::string>& args);
    // dbgmon [on|off]: 내 주변 섹터 몬스터 위치 구독 (인자 없으면 토글).
    CheatResult cheatDbgMon      (Stage& stage, const UserPtr& spUser, const std::vector<std::string>& args);

    // packet/packetdetail 공통 토글. all=true 면 글로벌, 아니면 호출 유저. level = 토글 대상(Name/Detail).
    CheatResult togglePacketLog(const UserPtr& spUser, bool all, EPacketLogMode level, const char* label);

    // 치트 이름 -> 핸들러.
    std::unordered_map<std::string, Handler> m_table;

    // ── 상태 ─────────────────────────────────────────────────
    std::atomic<bool> m_godMode{ false };

    // [치트] 글로벌 패킷 로깅 모드. 기본 None.
    EPacketLogMode m_globalPacketLogMode = EPacketLogMode::None;
};
