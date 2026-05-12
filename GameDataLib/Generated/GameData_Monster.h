#pragma once
// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 최초 1회만 생성됩니다.
// 이후에는 사용자가 직접 수정할 수 있습니다.
// =====================================================================

#include "GameDataBase_Monster.h"

// Monster 데이터 1건을 표현합니다.
struct GameData_Monster : public GameDataBase_Monster
{
public:
    bool Initialize();

};

// Monster 데이터 전체를 관리합니다.
class GameDataTable_Monster : public GameDataTableBase_Monster
{
public:
    virtual bool OnAddData(const GameData* pRawData) override;
    virtual bool OnLoadComplete() override;
};
