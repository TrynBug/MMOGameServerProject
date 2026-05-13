#pragma once
// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 최초 1회만 생성됩니다.
// 이후에는 사용자가 직접 수정할 수 있습니다.
// =====================================================================

#include "GameDataBase_Stage.h"


// Stage 데이터 1건을 표현합니다.
struct GameData_Stage : public GameDataBase_Stage
{
public:
    bool Initialize();

    // 여기에 사용자가 추가할 멤버변수, 멤버함수를 선언합니다.
};


// Stage 데이터 전체를 관리합니다.
class GameDataTable_Stage : public GameDataTableBase_Stage
{
public:
    GameDataTable_Stage() = default;
    ~GameDataTable_Stage() = default;

public:
    virtual bool OnAddData(const GameData* pRawData) override;
    virtual bool OnLoadComplete() override;

    // 여기에 사용자가 추가할 멤버함수를 선언합니다.
};
