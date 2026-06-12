#include "pch.h"
#include "StageScript.h"
#include "Stage.h"
#include "Monster.h"
#include "MonsterSpawner.h"
#include "Generated/GameData_Monster.h"

// 이 프로젝트는 NOMINMAX 를 정의하지 않아 Windows.h 의 min/max 매크로가 살아있다.
// sol2 헤더는 std::min/std::max 를 쓰므로 충돌한다. sol include 구간만 매크로를 잠시 끈다(전역 설정 불변).
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#include <sol/sol.hpp>
#pragma pop_macro("min")
#pragma pop_macro("max")

#include <filesystem>
#include <algorithm>
#include <unordered_set>

// ── pimpl: sol/Lua 타입을 .cpp 에 가둔다 ──────────────────────
struct StageScript::Impl
{
    // 주기/원샷 타이머. periodMs 마다 fn 호출. 컨텐츠 스레드 누적시간으로 만기 판정.
    struct Timer
    {
        int   id        = 0;
        int64 periodMs  = 0;
        int64 accumMs   = 0;
        bool  oneShot   = false;
        bool  cancelled = false;
        sol::protected_function fn;
    };

    // 진행 중인 시퀀스(코루틴). Wait(ms)/WaitForMonsterDead(key) 로 중단했다가 스케줄러가 재개한다.
    struct Sequence
    {
        enum class WaitKind { None, Time, Death, Count, SpawnerClear };

        sol::thread    runner;          // 코루틴 전용 lua_State 스택(독립 실행)
        sol::coroutine co;              // 실행 중인 코루틴
        WaitKind       waitKind = WaitKind::None;
        int64          waitRemainingMs   = 0;   // Time 대기 잔여(ms)
        int32          waitDeathKey      = 0;    // Death 대기 대상 monsterKey
        int            waitCountThreshold = 0;   // Count 대기: 생존수 <= 이 값이면 재개
        int32          waitSpawnerKey    = 0;    // SpawnerClear 대기: 이 스포너 생존 0이면 재개
        bool           done = false;
    };

    sol::state                    lua;
    std::vector<sol::environment> scripts;   // 스크립트별 환경 (environment 는 가벼운 참조형 → 값 보관)
    std::vector<Timer>            timers;
    std::vector<Sequence>         sequences;
    std::unordered_set<int32>     deathWatch;     // OnMonsterDead 발동 대상 monsterKey (Q4a)
    std::unordered_set<int32>     spawnerWatch;   // OnSpawnerMonsterDead 발동 대상 spawnerKey (Q4b)
    int                           nextTimerId = 1;
};

namespace
{
    // 1회 Lua 호출이 넘으면 안 되는 인스트럭션 상한 (무한루프가 컨텐츠 스레드를 영구 점유하는 것 차단).
    constexpr int k_instructionBudget = 5'000'000;

    // 인스트럭션 카운트 훅: budget 만큼 실행되면 호출되어 에러로 끊는다. (pcall 안이라 longjmp 안전.)
    void instructionGuardHook(lua_State* L, lua_Debug* /*ar*/)
    {
        luaL_error(L, "script instruction budget exceeded (possible infinite loop)");
    }

    void armGuard(sol::state& lua)
    {
        lua_sethook(lua.lua_state(), instructionGuardHook, LUA_MASKCOUNT, k_instructionBudget);
    }

    // yield 형식 (kind, arg) → outKind 코드: 0=time(ms), 1=death(key), 2=count(n), 3=spawnerclear(key).
    // 종료/에러면 done=true. Impl 타입 의존 없이 raw 인자만 받는다(헤더 노출 없음).
    void resumeCo(lua_State* threadState, sol::coroutine& co, int& outKind, int64& outArg, bool& done)
    {
        lua_sethook(threadState, instructionGuardHook, LUA_MASKCOUNT, k_instructionBudget);

        sol::protected_function_result r = co();

        if (co.status() == sol::call_status::yielded)
        {
            std::string kind = "time";
            int64       arg  = 0;
            if (r.return_count() >= 1)
            {
                sol::optional<std::string> k = r.get<sol::optional<std::string>>(0);
                if (k)
                    kind = *k;
            }
            if (r.return_count() >= 2)
            {
                sol::optional<int64> a = r.get<sol::optional<int64>>(1);
                if (a)
                    arg = *a;
            }
            if      (kind == "death")        outKind = 1;
            else if (kind == "count")        outKind = 2;
            else if (kind == "spawnerclear") outKind = 3;
            else                             outKind = 0;
            outArg = arg;
        }
        else
        {
            if (!r.valid())
            {
                sol::error e = r;
                LOG_WRITE(LogLevel::Error, std::format("Sequence error: {}", e.what()));
            }
            done = true;
        }
    }
}

StageScript::StageScript()
    : m_pImpl(std::make_unique<Impl>())
{
    // 경량 샌드박스: io/os/package 등 위험 라이브러리는 열지 않는다. (coroutine = 시퀀스 스폰용.)
    m_pImpl->lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string, sol::lib::coroutine);
}

StageScript::~StageScript() = default;

bool StageScript::Load(Stage& stage, const std::vector<std::string>& scriptNames)
{
    m_pStage = &stage;

    // 엔진 API. 전역 등록 → 모든 스크립트 환경이 _G 폴백으로 공유.
    StageScript* self = this;

    // 관측용 Log.
    m_pImpl->lua.set_function("Log", [](const std::string& msg)
    {
        LOG_WRITE(LogLevel::Info, std::format("[Lua] {}", msg));
    });

    // 타이머: 주기 호출. 컨텐츠 스레드(Update)에서 누적시간으로 만기 판정. timerId 반환.
    m_pImpl->lua.set_function("RegisterTimer", [self](int64 periodMs, sol::protected_function fn) -> int
    {
        Impl& impl = *self->m_pImpl;
        const int id = impl.nextTimerId++;
        impl.timers.push_back(Impl::Timer{ id, periodMs, 0, /*oneShot=*/false, /*cancelled=*/false, std::move(fn) });
        return id;
    });

    // 1회성 타이머(원샷). delayMs 후 1회 호출.
    m_pImpl->lua.set_function("SetTimeout", [self](int64 delayMs, sol::protected_function fn) -> int
    {
        Impl& impl = *self->m_pImpl;
        const int id = impl.nextTimerId++;
        impl.timers.push_back(Impl::Timer{ id, delayMs, 0, /*oneShot=*/true, /*cancelled=*/false, std::move(fn) });
        return id;
    });

    // 타이머 해제.
    m_pImpl->lua.set_function("CancelTimer", [self](int timerId)
    {
        for (auto& t : self->m_pImpl->timers)
            if (t.id == timerId)
                t.cancelled = true;
    });

    // 시퀀스용 yield 헬퍼. (StartSequence 안에서만 의미 있음.)
    //   Wait(ms)              = 현재 코루틴을 ms 동안 중단.
    //   WaitForMonsterDead(k) = key=k 몬스터가 죽을 때까지 중단.
    m_pImpl->lua.safe_script(
        "function Wait(ms) coroutine.yield('time', ms) end\n"
        "function WaitForMonsterDead(key) coroutine.yield('death', key) end\n"
        "function WaitForCount(n) coroutine.yield('count', n) end\n"
        "function WaitForSpawnerClear(key) coroutine.yield('spawnerclear', key) end");

    // 시퀀스 시작: fn 을 코루틴으로 실행. 첫 yield 까지 즉시 실행하고, 이후 스케줄러가 재개한다.
    m_pImpl->lua.set_function("StartSequence", [self](sol::protected_function fn)
    {
        Impl& impl = *self->m_pImpl;

        // 익명 함수를 러너 스레드에서 코루틴으로 바인딩 (전역 공유를 이용해 임시 키로 전달).
        impl.lua["__seq_fn_tmp"] = fn;
        Impl::Sequence seq;
        seq.runner = sol::thread::create(impl.lua.lua_state());
        seq.co     = sol::coroutine(seq.runner.state()["__seq_fn_tmp"]);
        impl.lua["__seq_fn_tmp"] = sol::lua_nil;

        impl.sequences.push_back(std::move(seq));
        self->advanceSequence(impl.sequences.size() - 1);   // 첫 yield 까지 실행 + 대기 상태 설정
    });

    // ── 스폰 / 스포너 / 조회 API (스폰 시스템 연동) ──
    Stage* stagePtr = m_pStage;

    // 즉시 스폰: (monsterKey, x, y, z) 에 1마리. NavMesh 스냅은 Stage::SpawnMonster 가 처리. objectId 반환(실패 0).
    m_pImpl->lua.set_function("SpawnMonster", [stagePtr](int32 monsterKey, float x, float y, float z) -> int64
    {
        Monster* p = stagePtr->SpawnMonster(monsterKey, x, y, z, 0.f);
        return p ? p->GetObjectId() : 0;
    });

    // 스포너 on/off (Manual 존 구동 등). MonsterSpawner 가 밀도/리스폰/팩 처리.
    m_pImpl->lua.set_function("ActivateSpawner", [stagePtr](int32 spawnerKey)
    {
        if (stagePtr->m_pSpawner)
            stagePtr->m_pSpawner->Activate(spawnerKey);
    });
    m_pImpl->lua.set_function("DeactivateSpawner", [stagePtr](int32 spawnerKey)
    {
        if (stagePtr->m_pSpawner)
            stagePtr->m_pSpawner->Deactivate(spawnerKey);
    });

    // 생존 몬스터 수 (웨이브 클리어 판정 등). 시체(IsDead)는 제외.
    m_pImpl->lua.set_function("GetAliveMonsterCount", [self]() -> int
    {
        return self->aliveMonsterCount();
    });

    // 사망 watch 등록/해제 (등록된 monsterKey 만 OnMonsterDead 발동 — 대량몹 부하 방지).
    m_pImpl->lua.set_function("WatchMonsterDeath", [self](int32 monsterKey)
    {
        self->m_pImpl->deathWatch.insert(monsterKey);
    });
    m_pImpl->lua.set_function("UnwatchMonsterDeath", [self](int32 monsterKey)
    {
        self->m_pImpl->deathWatch.erase(monsterKey);
    });

    // 스포너별 사망 watch: 그 스포너가 만든 몹이 죽으면 OnSpawnerMonsterDead 발동 (Q4b).
    m_pImpl->lua.set_function("WatchSpawnerDeath", [self](int32 spawnerKey)
    {
        self->m_pImpl->spawnerWatch.insert(spawnerKey);
    });
    m_pImpl->lua.set_function("UnwatchSpawnerDeath", [self](int32 spawnerKey)
    {
        self->m_pImpl->spawnerWatch.erase(spawnerKey);
    });

    const std::filesystem::path dir =
        std::filesystem::current_path().parent_path() / "Map" / "StageScript";

    for (const auto& name : scriptNames)
    {
        const std::filesystem::path path = dir / (name + ".lua");
        if (!std::filesystem::exists(path))
        {
            LOG_WRITE(LogLevel::Warn, std::format("StageScript file missing. name={} path={}", name, path.string()));
            continue;
        }

        // 1) 파일을 청크로 로드 (문법 검사).
        sol::load_result chunk = m_pImpl->lua.load_file(path.string());
        if (!chunk.valid())
        {
            sol::error e = chunk;
            LOG_WRITE(LogLevel::Error, std::format("StageScript compile failed. name={} err={}", name, e.what()));
            continue;
        }

        // 2) 스크립트별 환경(_ENV) 생성 후 청크에 주입 (전역 _G 공유, 콜백/지역상태는 환경에 격리).
        sol::environment env(m_pImpl->lua, sol::create, m_pImpl->lua.globals());
        sol::protected_function fn = chunk;
        sol::set_environment(env, fn);

        // 3) 청크 실행 (전역 함수 정의가 env 에 들어감).
        armGuard(m_pImpl->lua);
        sol::protected_function_result r = fn();
        if (!r.valid())
        {
            sol::error e = r;
            LOG_WRITE(LogLevel::Error, std::format("StageScript run failed. name={} err={}", name, e.what()));
            continue;
        }

        m_pImpl->scripts.push_back(std::move(env));
        LOG_WRITE(LogLevel::Info, std::format("StageScript loaded. stageId={} name={}", stage.GetStageId(), name));
    }
    return true;
}

void StageScript::Update(int64 deltaMs)
{
    auto& timers = m_pImpl->timers;

    // 인덱스 순회: 타이머 콜백이 RegisterTimer 로 새 타이머를 추가하면 vector 가 재할당될 수 있으므로
    // 참조를 길게 들지 않는다(필요 값은 호출 전에 복사). 새로 추가된 타이머는 이번 패스에선 accum=0.
    const size_t count = timers.size();
    for (size_t i = 0; i < count; ++i)
    {
        Impl::Timer& t = timers[i];
        if (t.cancelled)
            continue;

        t.accumMs += deltaMs;
        if (t.accumMs < t.periodMs)
            continue;

        // 발화: 만기 처리(원샷=취소 / 주기=누적 차감)를 콜백 호출 전에 끝낸다.
        sol::protected_function fn = t.fn;   // 복사 (재할당 대비)
        if (t.oneShot)
            t.cancelled = true;
        else
            t.accumMs -= t.periodMs;

        armGuard(m_pImpl->lua);
        sol::protected_function_result r = fn();
        if (!r.valid())
        {
            sol::error e = r;
            LOG_WRITE(LogLevel::Error, std::format("Timer callback error: {}", e.what()));
        }
    }

    // 취소된 타이머 정리.
    timers.erase(std::remove_if(timers.begin(), timers.end(),
        [](const Impl::Timer& t) { return t.cancelled; }), timers.end());

    // ── 시퀀스(코루틴) 시간/조건 대기 재개 ── (Death 대기는 CallOnMonsterDead 가 재개)
    using WaitKind = Impl::Sequence::WaitKind;
    auto& seqs = m_pImpl->sequences;
    const size_t seqCount = seqs.size();   // 재개 중 새 시퀀스가 추가되어도 이번 패스는 기존 것만.
    for (size_t i = 0; i < seqCount; ++i)
    {
        if (seqs[i].done)
            continue;

        bool ready = false;
        switch (seqs[i].waitKind)
        {
        case WaitKind::Time:
            seqs[i].waitRemainingMs -= deltaMs;
            ready = (seqs[i].waitRemainingMs <= 0);
            break;
        case WaitKind::Count:
            ready = (aliveMonsterCount() <= seqs[i].waitCountThreshold);
            break;
        case WaitKind::SpawnerClear:
            ready = (spawnerAliveCount(seqs[i].waitSpawnerKey) == 0);
            break;
        default:   // None / Death: 여기서 재개 안 함
            break;
        }

        if (ready)
            advanceSequence(i);
    }

    // 종료된 시퀀스 정리.
    seqs.erase(std::remove_if(seqs.begin(), seqs.end(),
        [](const Impl::Sequence& s) { return s.done; }), seqs.end());
}

int StageScript::aliveMonsterCount() const
{
    int n = 0;
    for (const auto& [objId, spObj] : m_pStage->m_monsterObjects)
        if (!static_cast<Monster*>(spObj.get())->IsDead())
            ++n;
    return n;
}

int StageScript::spawnerAliveCount(int32 spawnerKey) const
{
    int n = 0;
    for (const auto& [objId, spObj] : m_pStage->m_monsterObjects)
    {
        Monster* p = static_cast<Monster*>(spObj.get());
        if (!p->IsDead() && p->GetSpawnerKey() == spawnerKey)
            ++n;
    }
    return n;
}

// 코루틴을 1회 재개하고, 다음 대기 상태(Time/Death/Count/SpawnerClear) 또는 종료를 시퀀스에 반영한다.
void StageScript::advanceSequence(size_t index)
{
    using WaitKind = Impl::Sequence::WaitKind;
    auto& seqs = m_pImpl->sequences;

    // 재개 중 새 시퀀스 추가로 vector 가 재할당될 수 있으니, 핸들은 호출 전에 복사하고 결과는 재인덱싱하여 기록.
    lua_State*     ts = seqs[index].runner.thread_state();
    sol::coroutine co = seqs[index].co;

    int   kind = 0;
    int64 arg  = 0;
    bool  done = false;
    resumeCo(ts, co, kind, arg, done);

    Impl::Sequence& s = seqs[index];   // 재할당 대비 재인덱싱
    if (done)
    {
        s.done = true;
        return;
    }
    switch (kind)
    {
    case 1:  s.waitKind = WaitKind::Death;        s.waitDeathKey       = static_cast<int32>(arg); break;
    case 2:  s.waitKind = WaitKind::Count;        s.waitCountThreshold = static_cast<int>(arg);   break;
    case 3:  s.waitKind = WaitKind::SpawnerClear; s.waitSpawnerKey     = static_cast<int32>(arg); break;
    default: s.waitKind = WaitKind::Time;         s.waitRemainingMs    = arg;                     break;
    }
}

void StageScript::CallOnStageStart()
{
    for (auto& env : m_pImpl->scripts)
    {
        sol::protected_function fn = env["OnStageStart"];
        if (!fn.valid())
            continue;

        armGuard(m_pImpl->lua);
        sol::protected_function_result r = fn();
        if (!r.valid())
        {
            sol::error e = r;
            LOG_WRITE(LogLevel::Error, std::format("OnStageStart error: {}", e.what()));
        }
    }
}

void StageScript::CallOnPlayerEnter(int64 userId)
{
    for (auto& env : m_pImpl->scripts)
    {
        sol::protected_function fn = env["OnPlayerEnter"];
        if (!fn.valid())
            continue;

        armGuard(m_pImpl->lua);
        sol::protected_function_result r = fn(userId);
        if (!r.valid())
        {
            sol::error e = r;
            LOG_WRITE(LogLevel::Error, std::format("OnPlayerEnter error: {}", e.what()));
        }
    }
}

void StageScript::CallOnPlayerLeave(int64 userId)
{
    for (auto& env : m_pImpl->scripts)
    {
        sol::protected_function fn = env["OnPlayerLeave"];
        if (!fn.valid())
            continue;

        armGuard(m_pImpl->lua);
        sol::protected_function_result r = fn(userId);
        if (!r.valid())
        {
            sol::error e = r;
            LOG_WRITE(LogLevel::Error, std::format("OnPlayerLeave error: {}", e.what()));
        }
    }
}

void StageScript::CallOnMonsterDead(int64 objectId, int32 monsterKey, int32 spawnerKey, int64 killerObjectId)
{
    // 1) 이 key 의 사망을 기다리던 시퀀스(WaitForMonsterDead) 재개. (watch 와 무관 — 시퀀스가 자기 대기를 설정.)
    {
        auto& seqs = m_pImpl->sequences;
        const size_t n = seqs.size();   // 재개 중 추가된 시퀀스는 이번 이벤트 대상 아님.
        for (size_t i = 0; i < n; ++i)
        {
            if (!seqs[i].done
                && seqs[i].waitKind == Impl::Sequence::WaitKind::Death
                && seqs[i].waitDeathKey == monsterKey)
            {
                advanceSequence(i);
            }
        }
        seqs.erase(std::remove_if(seqs.begin(), seqs.end(),
            [](const Impl::Sequence& s) { return s.done; }), seqs.end());
    }

    // 2) watch 등록된 monsterKey 만 OnMonsterDead 콜백.
    if (m_pImpl->deathWatch.find(monsterKey) != m_pImpl->deathWatch.end())
    {
        for (auto& env : m_pImpl->scripts)
        {
            sol::protected_function fn = env["OnMonsterDead"];
            if (!fn.valid())
                continue;

            armGuard(m_pImpl->lua);
            sol::protected_function_result r = fn(objectId, monsterKey, killerObjectId);
            if (!r.valid())
            {
                sol::error e = r;
                LOG_WRITE(LogLevel::Error, std::format("OnMonsterDead error: {}", e.what()));
            }
        }
    }

    // 3) watch 등록된 spawnerKey 의 몹이 죽으면 OnSpawnerMonsterDead 콜백 (Q4b).
    if (spawnerKey != 0 && m_pImpl->spawnerWatch.find(spawnerKey) != m_pImpl->spawnerWatch.end())
    {
        for (auto& env : m_pImpl->scripts)
        {
            sol::protected_function fn = env["OnSpawnerMonsterDead"];
            if (!fn.valid())
                continue;

            armGuard(m_pImpl->lua);
            sol::protected_function_result r = fn(spawnerKey, objectId, monsterKey);
            if (!r.valid())
            {
                sol::error e = r;
                LOG_WRITE(LogLevel::Error, std::format("OnSpawnerMonsterDead error: {}", e.what()));
            }
        }
    }
}
