#pragma once

#include <string>

// 게임데이터 기본클래스
struct GameData
{
    
};

// 게임데이터파일 기본클래스
class GameDataTable
{
public:
    GameDataTable() = default;
    virtual ~GameDataTable() = default;

public:
    static const std::string& GetDataFilePath() { return sm_dataFilePath; }

    static bool StringToBool(const std::string& str);

public:
    bool LoadData(const std::string& csvPath);

public:
    virtual const char* GetDataName() = 0;

    virtual bool OnAddData(const GameData* pRawData) = 0;
    virtual bool OnLoadComplete() = 0;

protected:
    virtual bool makeGameData(const std::string& line) = 0;

protected:
    inline static std::string sm_dataFilePath; // 읽은 데이터 파일명(경로포함)
};