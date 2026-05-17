#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cstdint>
#include <format>

#include "Logger.h"

#include "GameData.h"



bool GameDataTable::LoadData(const std::string& csvPath)
{
    char lastChar = csvPath.back();
    if (lastChar != '/' && lastChar != '\\') 
    {
        m_dataFilePath = csvPath + "/" + GetDataName() + ".csv";
    }
    else
    {
        m_dataFilePath = csvPath + GetDataName() + ".csv";
    }

    std::ifstream file(m_dataFilePath);

    if (!file.is_open()) 
    {
		LOG_WRITE(LogLevel::Error, std::format("데이터파일을 열 수 없습니다. Path={}", m_dataFilePath));
        return false;
    }

    std::string line;
    
    // 첫 번째 행(헤더) 건너뛰기
    std::getline(file, line);

    // 한 행씩 읽기
    while (std::getline(file, line)) 
    {
        if (line.empty()) 
            continue;

        if (false == makeGameData(line)) // 이 함수 내에서 GameData를 만들고, 초기화하고, map에 등록한다.
        {
            file.close();
            return false;
        }
    }

    file.close();

    return true;
}

// string to boolean
bool GameDataTable::StringToBool(const std::string& str)
{
    return (str == "true" || str == "True" || str == "TRUE");
}