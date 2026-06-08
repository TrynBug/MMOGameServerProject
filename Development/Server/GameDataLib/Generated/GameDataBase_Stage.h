#pragma once
// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 자동 생성됩니다.
// 직접 수정하지 마세요. 데이터 .xlsx 가 변경되면 재생성됩니다.
// =====================================================================

#include <map>
#include <string>

#include "LoggerLib.h"
#include "../GameData.h"

#include "Enum/GameEnum_Stage.h"

struct GameData_Stage;


// Stage 데이터 1개 행을 표현합니다.
struct GameDataBase_Stage : public GameData
{
    int32_t              Key                  = 0;
    EStageType           StageType            = EStageType::None;
    std::string          NavMeshFileName      = "";
    float                sectorSize           = 0.0f;
};


// Stage 데이터 파일 전체를 표현합니다.
class GameDataTableBase_Stage : public GameDataTable
{

public:
    static constexpr const std::string_view k_dataName = "Stage";

protected:
    GameDataTableBase_Stage() = default;
    virtual ~GameDataTableBase_Stage() = default;

public:
    static const GameData_Stage* FindData(int32_t key);
    static const std::map<int32_t, const GameData_Stage*>& GetDataMap() { return sm_dataMap; }

public:
    const char* GetDataName() override { return "Stage"; }

protected:
    virtual bool makeGameData(const std::string& line) override;

protected:
    inline static std::map<int32_t, const GameData_Stage*> sm_dataMap;
};
