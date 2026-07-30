#pragma once
// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include <map>
#include <string>

#include "LoggerLib.h"
#include "../GameData.h"


struct GameData_Item;


// Item 데이터 1개 행을 표현합니다.
struct GameDataBase_Item : public GameData
{
    int32_t              Key                  = 0;
    std::string          Name                 = "";
    int32_t              ItemType             = 0;
    int32_t              Grade                = 0;
    int32_t              MaxStack             = 1;
};


// Item 데이터 파일 전체를 표현합니다.
class GameDataTableBase_Item : public GameDataTable
{

public:
    static constexpr const std::string_view k_dataName = "Item";

protected:
    GameDataTableBase_Item() = default;
    virtual ~GameDataTableBase_Item() = default;

public:
    static const GameData_Item* FindData(int32_t key);
    static const std::map<int32_t, const GameData_Item*>& GetDataMap() { return sm_dataMap; }

public:
    const char* GetDataName() override { return "Item"; }

protected:
    virtual bool makeGameData(const std::string& line) override;

protected:
    inline static std::map<int32_t, const GameData_Item*> sm_dataMap;
};
