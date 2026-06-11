#include "pch.h"
#include "StageLayout.h"

#include "nlohmann/json.hpp"

#include <filesystem>
#include <fstream>

bool StageLayout::Load(int32 stageDataKey)
{
    // 런타임 작업 디렉터리(exe 위치 = OUTPUT/<Config>) 기준. NavMesh 가 Map/NavMesh 인 것과 동형.
    const std::filesystem::path path =
        std::filesystem::current_path().parent_path() / "Map" / "StageLayout" / (std::to_string(stageDataKey) + ".json");

    if (!std::filesystem::exists(path))
    {
        // 레이아웃 없는 Stage 는 정상(스포너 0개). 에러 아님.
        LOG_WRITE(LogLevel::Info, std::format("StageLayout none. stageDataKey={} path={}", stageDataKey, path.string()));
        return true;
    }

    try
    {
        std::ifstream ifs(path);
        nlohmann::json j;
        ifs >> j;

        if (j.contains("spawners"))
        {
            for (const auto& s : j["spawners"])
            {
                SpawnerPlacement sp;
                sp.key  = s.at("key").get<int32>();
                const auto& pos = s.at("pos");
                sp.posX = pos.at(0).get<float>();
                sp.posY = pos.at(1).get<float>();
                sp.posZ = pos.at(2).get<float>();
                sp.radius = s.value("radius", 0.0f);
                m_spawners.push_back(sp);
            }
        }
    }
    catch (const std::exception& e)
    {
        LOG_WRITE(LogLevel::Error, std::format("StageLayout parse failed. stageDataKey={} err={}", stageDataKey, e.what()));
        return false;
    }

    LOG_WRITE(LogLevel::Info, std::format("StageLayout loaded. stageDataKey={} spawners={}", stageDataKey, m_spawners.size()));
    return true;
}
