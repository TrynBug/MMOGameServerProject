#include "pch.h"
#include "Managers/CastAnchorRegistry.h"

#include "nlohmann/json.hpp"

#include <filesystem>
#include <fstream>
#include <cmath>

namespace
{
    std::filesystem::path castAnchorPath()
    {
        return std::filesystem::current_path().parent_path() / "Map" / "CastAnchors.json";
    }
}

bool CastAnchorRegistry::Load()
{
    m_playerOffsets.clear();
    m_prefabOffsets.clear();

    const std::filesystem::path path = castAnchorPath();
    if (!std::filesystem::exists(path))
    {
        LOG_WRITE(LogLevel::Warn, std::format("CastAnchors file not found. Using actor-center fallback. path={}", path.string()));
        return true;
    }

    try
    {
        std::ifstream ifs(path);
        nlohmann::json json;
        ifs >> json;

        const int schemaVersion = json.value("schemaVersion", 0);
        if (schemaVersion != 3)
        {
            LOG_WRITE(LogLevel::Error, std::format("CastAnchors unsupported schema. path={}", path.string()));
            return false;
        }

        if (json.contains("players"))
        {
            for (const auto& entry : json.at("players"))
            {
                const int32 job = entry.at("job").get<int32>();
                const int32 preset = entry.at("preset").get<int32>();
                const Vector3 offset(entry.at("localOffset").at("x").get<float>(),
                                     entry.at("localOffset").at("y").get<float>(),
                                     entry.at("localOffset").at("z").get<float>());
                if (!std::isfinite(offset.x) || !std::isfinite(offset.y) || !std::isfinite(offset.z)
                    || !m_playerOffsets.emplace(makePlayerKey(job, preset), offset).second)
                {
                    LOG_WRITE(LogLevel::Error, std::format("CastAnchors invalid/duplicate player entry. job={} preset={}", job, preset));
                    return false;
                }
            }
        }

        if (schemaVersion >= 3 && json.contains("prefabs"))
        {
            for (const auto& entry : json.at("prefabs"))
            {
                const std::string prefabPath = entry.at("prefabPath").get<std::string>();
                const Vector3 offset(entry.at("localOffset").at("x").get<float>(),
                                     entry.at("localOffset").at("y").get<float>(),
                                     entry.at("localOffset").at("z").get<float>());
                if (prefabPath.empty() || !std::isfinite(offset.x) || !std::isfinite(offset.y) || !std::isfinite(offset.z)
                    || !m_prefabOffsets.emplace(prefabPath, offset).second)
                {
                    LOG_WRITE(LogLevel::Error, std::format("CastAnchors invalid/duplicate prefab entry. prefab={}", prefabPath));
                    return false;
                }
            }
        }

    }
    catch (const std::exception& e)
    {
        LOG_WRITE(LogLevel::Error, std::format("CastAnchors parse failed. path={} err={}", path.string(), e.what()));
        return false;
    }

    LOG_WRITE(LogLevel::Info, std::format("CastAnchors loaded. prefabs={} players={}", m_prefabOffsets.size(), m_playerOffsets.size()));
    return true;
}

Vector3 CastAnchorRegistry::GetPlayerLocalOffset(int32 jobId, int32 presetId) const
{
    const auto it = m_playerOffsets.find(makePlayerKey(jobId, presetId));
    return it != m_playerOffsets.end() ? it->second : Vector3();
}

Vector3 CastAnchorRegistry::GetMonsterLocalOffset(std::string_view prefabPath) const
{
    const auto prefabIt = m_prefabOffsets.find(std::string(prefabPath));
    return prefabIt != m_prefabOffsets.end() ? prefabIt->second : Vector3();
}

uint64 CastAnchorRegistry::makePlayerKey(int32 jobId, int32 presetId)
{
    return (static_cast<uint64>(static_cast<uint32>(jobId)) << 32)
         | static_cast<uint32>(presetId);
}
