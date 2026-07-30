#pragma once
// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include <map>
#include <string>

#include "LoggerLib.h"
#include "../GameData.h"


struct GameData_ItemDrop;


// ItemDrop 데이터 1개 행을 표현합니다.
struct GameDataBase_ItemDrop : public GameData
{
    int32_t              Key                  = 0;
    int32_t              GroupKey             = 0;
    int32_t              ItemKey              = 0;
    int32_t              ChancePermyriad      = 0;
    int32_t              MinCount             = 1;
    int32_t              MaxCount             = 1;
};


// ItemDrop 데이터 파일 전체를 표현합니다.
class GameDataTableBase_ItemDrop : public GameDataTable
{

public:
    static constexpr const std::string_view k_dataName = "ItemDrop";

protected:
    GameDataTableBase_ItemDrop() = default;
    virtual ~GameDataTableBase_ItemDrop() = default;

public:
    static const GameData_ItemDrop* FindData(int32_t key);
    static const std::map<int32_t, const GameData_ItemDrop*>& GetDataMap() { return sm_dataMap; }

public:
    const char* GetDataName() override { return "ItemDrop"; }

protected:
    virtual bool makeGameData(const std::string& line) override;

protected:
    inline static std::map<int32_t, const GameData_ItemDrop*> sm_dataMap;
};
