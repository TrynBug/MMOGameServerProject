#include "pch.h"
#include "Stages/StageLayout.h"

#include "nlohmann/json.hpp"

#include <filesystem>
#include <fstream>

bool StageLayout::Load(const std::string& stageLayoutFileName)
{
    // 레이아웃 파일명은 GameData_Stage.StageLayoutFileName 기준(예: Town.json). NavMesh 파일명과 동일하게 쓰는 컨벤션.
    // 같은 StageLayoutFileName 을 공유하는 Stage 들은 같은 레이아웃을 쓴다.
    if (stageLayoutFileName.empty())
        return true;   // 레이아웃 없는 Stage(SystemStage 등) — 빈 레이아웃.

    // 런타임 작업 디렉터리(exe 위치 = OUTPUT/<Config>) 기준. NavMesh 가 Map/NavMesh 인 것과 동형.
    const std::filesystem::path path =
        std::filesystem::current_path().parent_path() / "Map" / "StageLayout" / (stageLayoutFileName + ".json");

    if (!std::filesystem::exists(path))
    {
        // 파일명이 명시됐는데(StageLayoutFileName 비어있지 않음) 실제 파일이 없으면 시작 실패(스크립트와 동일 fail-fast).
        LOG_WRITE(LogLevel::Error, std::format("StageLayout file not found. file={} path={}", stageLayoutFileName, path.string()));
        return false;
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

        if (j.contains("spawnPoints"))
        {
            for (const auto& s : j["spawnPoints"])
            {
                SpawnPoint sp;
                sp.key  = s.at("key").get<int32>();
                const auto& pos = s.at("pos");
                sp.posX = pos.at(0).get<float>();
                sp.posY = pos.at(1).get<float>();
                sp.posZ = pos.at(2).get<float>();
                sp.yaw  = s.value("yaw", 0.0f);
                m_spawnPoints.push_back(sp);
            }
        }

        if (j.contains("waypoints"))
        {
            for (const auto& w : j["waypoints"])
            {
                Waypoint wp;
                wp.key = w.at("key").get<int32>();
                for (const auto& pt : w.at("points"))
                    wp.points.push_back({ pt.at(0).get<float>(), pt.at(1).get<float>(), pt.at(2).get<float>() });
                m_waypoints.push_back(std::move(wp));
            }
        }

        if (j.contains("eventAreas"))
        {
            for (const auto& e : j["eventAreas"])
            {
                EventArea ea;
                ea.key = e.at("key").get<int32>();
                const std::string shape = e.value("shape", std::string("Sphere"));
                ea.shape = (shape == "Box") ? 1 : 0;
                const auto& center = e.at("center");
                ea.cx = center.at(0).get<float>();
                ea.cy = center.at(1).get<float>();
                ea.cz = center.at(2).get<float>();
                ea.radius = e.value("radius", 0.0f);
                if (e.contains("size"))   // Box 전체 크기 [x, y, z] (평면 판정엔 x, z 만 사용)
                {
                    const auto& size = e.at("size");
                    ea.sizeX = size.at(0).get<float>();
                    ea.sizeZ = size.at(2).get<float>();
                }
                ea.secure = e.value("secure", false);
                m_eventAreas.push_back(ea);
            }
        }

        if (j.contains("props"))
        {
            for (const auto& p : j["props"])
            {
                Prop pr;
                pr.key  = p.at("key").get<int32>();
                pr.type = p.value("type", 0);
                const auto& pos = p.at("pos");
                pr.x = pos.at(0).get<float>();
                pr.y = pos.at(1).get<float>();
                pr.z = pos.at(2).get<float>();
                pr.range = p.value("range", 2.0f);
                m_props.push_back(pr);
            }
        }
    }
    catch (const std::exception& e)
    {
        LOG_WRITE(LogLevel::Error, std::format("StageLayout parse failed. file={} err={}", stageLayoutFileName, e.what()));
        return false;
    }

    LOG_WRITE(LogLevel::Info, std::format("StageLayout loaded. file={} spawners={} spawnPoints={} waypoints={} eventAreas={} props={}",
        stageLayoutFileName, m_spawners.size(), m_spawnPoints.size(), m_waypoints.size(), m_eventAreas.size(), m_props.size()));
    return true;
}

const StageLayout::SpawnPoint* StageLayout::GetSpawnPoint(int32 key) const
{
    for (const auto& sp : m_spawnPoints)
        if (sp.key == key)
            return &sp;
    return nullptr;
}

const StageLayout::Waypoint* StageLayout::GetWaypoint(int32 key) const
{
    for (const auto& wp : m_waypoints)
        if (wp.key == key)
            return &wp;
    return nullptr;
}

const StageLayout::EventArea* StageLayout::GetEventArea(int32 key) const
{
    for (const auto& ea : m_eventAreas)
        if (ea.key == key)
            return &ea;
    return nullptr;
}

const StageLayout::Prop* StageLayout::GetProp(int32 key) const
{
    for (const auto& pr : m_props)
        if (pr.key == key)
            return &pr;
    return nullptr;
}
