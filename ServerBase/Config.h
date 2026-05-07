#pragma once

#include "pch.h"

#include <fstream>
#include <sstream>
#include <unordered_map>

namespace serverbase
{

// INI 형식 설정 파일 파서
// [Section]
// Key=Value  형식을 지원한다.
// 
// 사용 예:
//   Config cfg;
//   cfg.Load("server.ini");
//   int32 port = cfg.GetInt32("Network", "Port", 8080);
//   std::string ip = cfg.GetString("Network", "IP", "0.0.0.0");
class Config
{
public:
    bool Load(const std::string& filePath);

    std::string GetString(const std::string& section, const std::string& key, const std::string& defaultValue = "") const;
    int32       GetInt32 (const std::string& section, const std::string& key, int32 defaultValue = 0) const;
    int64       GetInt64 (const std::string& section, const std::string& key, int64 defaultValue = 0) const;
    bool        GetBool  (const std::string& section, const std::string& key, bool  defaultValue = false) const;

private:
    // m_data[section][key] = value
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> m_data;

    static std::string trim(const std::string& s);
};

} // namespace serverbase
