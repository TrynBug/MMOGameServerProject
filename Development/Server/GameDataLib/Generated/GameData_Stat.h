#pragma once
// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 최초 1회만 생성됩니다.
// 이후에는 사용자가 직접 수정할 수 있습니다.
// =====================================================================

#include <array>

#include "GameDataBase_Stat.h"


// Stat 데이터 1건을 표현합니다.
struct GameData_Stat : public GameDataBase_Stat
{
public:
    bool Initialize();

    // 여기에 사용자가 추가할 멤버변수, 멤버함수를 선언합니다.
};


// 한 스탯 그룹(예: 힘)을 구성하는 스탯들의 역인덱스.
// 총합(Total) 재계산 시 이 정보로 raw 스탯들을 Op 순서대로 합성한다.
//   slot[Op] : 해당 Op 의 raw 스탯 (예: slot[Add]=StrAdd). 없으면 EStat::None.
//   total    : 이 그룹의 총합 스탯 (Op=Total). 없으면 EStat::None.
struct StatGroupInfo
{
    EStat total = EStat::None;
    std::array<EStat, static_cast<size_t>(EStatOp::Max)> slot = {};   // 모두 EStat::None 으로 초기화
};


// Stat 데이터 전체를 관리합니다.
//
// GameDataTableBase_Stat 은 Key(int64) -> GameData_Stat* 매핑(sm_dataMap)을 제공한다.
// 여기서는 추가로 스탯 시스템이 쓰는 역인덱스 3종을 OnLoadComplete 시점에 구성한다.
//   - sm_dataByStatMap : EStat        -> 데이터        (개별 스탯의 Min/Max/Op 등 조회)
//   - sm_groupInfo      : EStatGroup   -> 구성 스탯들   (총합 재계산용)
//   - sm_statToGroup    : EStat        -> 소속 그룹     (raw 스탯 갱신 시 어느 그룹을 재계산할지)
// 이 역인덱스들은 모든 액터가 공유하는 정적 const 데이터다. 로드 후에는 변경되지 않는다.
class GameDataTable_Stat : public GameDataTableBase_Stat
{
public:
    GameDataTable_Stat() = default;
    ~GameDataTable_Stat() = default;

public:
    virtual bool OnAddData(const GameData* pRawData) override;
    virtual bool OnLoadComplete() override;

    // 여기에 사용자가 추가할 멤버함수를 선언합니다.

public:
    // ── 역인덱스 조회 API ─────────────────────────────────────────
    // EStat 으로 데이터 조회 (sm_dataMap 의 Key 가 int64 라서 별도 맵 제공).
    static const GameData_Stat* FindDataByStat(EStat stat);

    // 그룹의 구성 스탯 정보 (총합 재계산용). 유효하지 않은 그룹이면 nullptr.
    static const StatGroupInfo* GetGroupInfo(EStatGroup group);

    // 스탯이 속한 그룹 (raw 스탯 갱신 시 어느 그룹 재계산할지). 없으면 EStatGroup::None.
    static EStatGroup GetStatGroup(EStat stat);

private:
    // OnLoadComplete 에서 sm_dataMap 을 순회하여 아래 역인덱스들을 구성한다.
    bool buildStatIndex();

private:
    inline static std::map<EStat, const GameData_Stat*> sm_dataByStatMap;
    inline static std::array<StatGroupInfo, static_cast<size_t>(EStatGroup::Max)> sm_groupInfo;
    inline static std::array<EStatGroup, static_cast<size_t>(EStat::Max)> sm_statToGroup = {};   // 모두 None 으로 초기화
};
