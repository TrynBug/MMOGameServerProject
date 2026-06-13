#include "pch.h"
#include "Managers/StageAssetManager.h"

#include "Generated/GameData_Stage.h"

// Lua C API (바이트코드 컴파일/덤프용). sol 불필요 → min/max 매크로 이슈 없음(순수 C 헤더).
#include <lua.hpp>

#include <filesystem>

namespace
{
    // lua_dump writer: 바이트코드 청크를 std::string 에 누적.
    int dumpWriter(lua_State* /*L*/, const void* p, size_t sz, void* ud)
    {
        static_cast<std::string*>(ud)->append(static_cast<const char*>(p), sz);
        return 0;
    }

    std::filesystem::path scriptPath(const std::string& name)
    {
        // StageScript 와 동일 규약: Map/StageScript/<이름>.lua (런타임 작업디렉터리 기준).
        return std::filesystem::current_path().parent_path() / "Map" / "StageScript" / (name + ".lua");
    }
}

bool StageAssetManager::LoadAll()
{
    bool ok = true;

    for (const auto& [stageDataKey, pStageData] : GameDataTable_Stage::GetDataMap())
    {
        // 1) 레이아웃 — 파일명은 GameData_Stage.StageLayoutFileName 기준(예: Town.json).
        //    이름이 비면 빈 레이아웃(정상). 이름이 명시됐는데 파일이 없거나 파싱 실패면 시작 실패(스크립트와 동일 fail-fast).
        //    m_layouts 는 stageDataKey 키 유지(인스턴스 공유). 같은 StageLayoutFileName 을 쓰는 Stage 는 같은 파일을 읽는다.
        StageLayout layout;
        if (!layout.Load(pStageData->StageLayoutFileName))
            ok = false;
        m_layouts.emplace(stageDataKey, std::move(layout));

        // 2) 명시된 스크립트(ScriptName#) — 이름별 1회 컴파일. 누락/실패면 시작 실패.
        for (int32 i = 0; i < pStageData->GetScriptNameCount(); ++i)
        {
            const std::string name = pStageData->GetScriptName(i);
            if (name.empty())
                continue;
            if (m_scriptBytecode.find(name) != m_scriptBytecode.end())
                continue;   // 이미 컴파일됨(여러 Stage 가 같은 스크립트 공유)
            if (!compileScript(name))
                ok = false;
        }
    }

    LOG_WRITE(LogLevel::Info, std::format("StageAssetManager loaded. layouts={} scripts={} ok={}",
        m_layouts.size(), m_scriptBytecode.size(), ok));
    return ok;
}

bool StageAssetManager::compileScript(const std::string& name)
{
    const std::filesystem::path path = scriptPath(name);
    if (!std::filesystem::exists(path))
    {
        LOG_WRITE(LogLevel::Error, std::format("StageScript file missing. name={} path={}", name, path.string()));
        return false;
    }

    lua_State* L = luaL_newstate();
    if (!L)
    {
        LOG_WRITE(LogLevel::Error, "luaL_newstate failed (script compile)");
        return false;
    }

    bool ok = true;
    if (luaL_loadfile(L, path.string().c_str()) != LUA_OK)
    {
        const char* err = lua_tostring(L, -1);
        LOG_WRITE(LogLevel::Error, std::format("StageScript compile failed. name={} err={}", name, err ? err : "?"));
        ok = false;
    }
    else
    {
        // 로드된(아직 실행 안 한) 청크를 바이트코드로 덤프. strip=0: 디버그 라인정보 유지(에러 메시지용).
        std::string bytecode;
        lua_dump(L, dumpWriter, &bytecode, /*strip=*/0);
        const size_t bytes = bytecode.size();
        m_scriptBytecode.emplace(name, std::move(bytecode));
        LOG_WRITE(LogLevel::Info, std::format("StageScript compiled. name={} bytes={}", name, bytes));
    }

    lua_close(L);
    return ok;
}

const StageLayout* StageAssetManager::FindLayout(int32 stageDataKey) const
{
    auto it = m_layouts.find(stageDataKey);
    return (it != m_layouts.end()) ? &it->second : nullptr;
}

const std::string* StageAssetManager::FindScriptBytecode(const std::string& name) const
{
    auto it = m_scriptBytecode.find(name);
    return (it != m_scriptBytecode.end()) ? &it->second : nullptr;
}
