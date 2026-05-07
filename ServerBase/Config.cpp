#include "pch.h"
#include "Config.h"
#include "Logger.h"

#include <fstream>
#include <sstream>

namespace serverbase
{

bool Config::Load(const std::string& filePath)
{
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        LOG_ERROR("Config::Load - cannot open file: " + filePath);
        return false;
    }

    std::string currentSection;
    std::string line;

    while (std::getline(file, line))
    {
        line = trim(line);

        // 빈 줄 또는 주석
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;

        // 섹션
        if (line.front() == '[' && line.back() == ']')
        {
            currentSection = trim(line.substr(1, line.size() - 2));
            continue;
        }

        // Key=Value
        auto eqPos = line.find('=');
        if (eqPos == std::string::npos)
            continue;

        std::string key   = trim(line.substr(0, eqPos));
        std::string value = trim(line.substr(eqPos + 1));

        // 인라인 주석 제거 (# 또는 ;)
        for (char commentChar : {'#', ';'})
        {
            auto commentPos = value.find(commentChar);
            if (commentPos != std::string::npos)
                value = trim(value.substr(0, commentPos));
        }

        m_data[currentSection][key] = value;
    }

    LOG_INFO("Config loaded: " + filePath);
    return true;
}

std::string Config::GetString(const std::string& section, const std::string& key, const std::string& defaultValue) const
{
    auto secIt = m_data.find(section);
    if (secIt == m_data.end())
        return defaultValue;

    auto keyIt = secIt->second.find(key);
    if (keyIt == secIt->second.end())
        return defaultValue;

    return keyIt->second;
}

int32 Config::GetInt32(const std::string& section, const std::string& key, int32 defaultValue) const
{
    std::string val = GetString(section, key);
    if (val.empty())
        return defaultValue;

    try   
    { 
        return static_cast<int32>(std::stoi(val)); 
    }
    catch (...) 
    { 
        return defaultValue; 
    }
}

int64 Config::GetInt64(const std::string& section, const std::string& key, int64 defaultValue) const
{
    std::string val = GetString(section, key);
    if (val.empty())
        return defaultValue;

    try   
    { 
        return std::stoll(val); 
    }
    catch (...) 
    { 
        return defaultValue; 
    }
}

bool Config::GetBool(const std::string& section, const std::string& key, bool defaultValue) const
{
    std::string val = GetString(section, key);
    if (val.empty())
        return defaultValue;

    // true/false/1/0/yes/no
    if (val == "true"  || val == "1" || val == "yes") 
        return true;

    if (val == "false" || val == "0" || val == "no")  
        return false;

    return defaultValue;
}

std::string Config::trim(const std::string& s)
{
    const std::string whitespace = " \t\r\n";
    auto start = s.find_first_not_of(whitespace);
    if (start == std::string::npos)
        return "";

    auto end = s.find_last_not_of(whitespace);
    return s.substr(start, end - start + 1);
}

} // namespace serverbase
