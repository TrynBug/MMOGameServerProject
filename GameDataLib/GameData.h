#pragma once

#include <string>

// 게임데이터 기본클래스
struct GameData
{
    
};

// 게임데이터파일 기본클래스
class GameDataTable
{
protected:
    GameDataTable() = default;
    virtual ~GameDataTable() = default;

public:
    bool LoadData(const std::string& csvPath);

    std::string& GetDataFilePath() { return m_dataFilePath; }
    bool StringToBool(const std::string& str);

public:
    virtual const char* GetDataName() = 0;

    virtual bool OnAddData(const GameData* pRawData) = 0;
    virtual bool OnLoadComplete() = 0;

protected:
    virtual bool makeGameData(const std::string& line) = 0;

protected:
    std::string m_dataFilePath; // 읽은 데이터 파일명(경로포함)
};