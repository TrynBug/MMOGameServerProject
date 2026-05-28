#pragma once
// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 최초 1회만 생성됩니다.
// 이후에는 사용자가 직접 수정할 수 있습니다.
// =====================================================================

#include "GameDataBase_JobBase.h"


// JobBase 데이터 1건을 표현합니다.
struct GameData_JobBase : public GameDataBase_JobBase
{
public:
    bool Initialize();

    // 여기에 사용자가 추가할 멤버변수, 멤버함수를 선언합니다.
};


// JobBase 데이터 전체를 관리합니다.
//
// GameDataTableBase_JobBase 는 Key(int64) -> GameData_JobBase* 매핑(sm_dataMap)을 제공한다.
// 여기서는 EJob 으로 직접 조회할 수 있도록 OnLoadComplete 시점에 별도 맵을 구성한다.
class GameDataTable_JobBase : public GameDataTableBase_JobBase
{
public:
    GameDataTable_JobBase() = default;
    ~GameDataTable_JobBase() = default;

public:
    virtual bool OnAddData(const GameData* pRawData) override;
    virtual bool OnLoadComplete() override;

    // 여기에 사용자가 추가할 멤버함수를 선언합니다.

public:
    // EJob 으로 데이터 조회 (sm_dataMap 의 Key 가 int64 라서 별도 맵 제공).
    static const GameData_JobBase* FindDataByJob(EJob job);

private:
    inline static std::map<EJob, const GameData_JobBase*> sm_dataByJobMap;
};
