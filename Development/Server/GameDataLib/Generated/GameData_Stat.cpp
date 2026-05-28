// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 최초 1회만 생성됩니다.
// 이후에는 사용자가 직접 수정할 수 있습니다.
// =====================================================================

#include <format>

#include "LoggerLib.h"
#include "GameData_Stat.h"


bool GameData_Stat::Initialize()
{
    // 데이터 1건을 읽은 직후 호출됨

    return true;
}

bool GameDataTable_Stat::OnAddData(const GameData* pRawData)
{
    const GameData_Stat* pData = static_cast<const GameData_Stat*>(pRawData);

    // 데이터가 sm_dataMap 에 추가된 후 호출됨

    return true;
}

bool GameDataTable_Stat::OnLoadComplete()
{
    // 전체 데이터 로드 완료 후, 스탯 시스템이 쓰는 역인덱스를 구성한다.
    return buildStatIndex();
}

bool GameDataTable_Stat::buildStatIndex()
{
    sm_dataByStatMap.clear();

    for (const auto& [key, pData] : sm_dataMap)
    {
        const EStat stat = pData->Stat;
        const EStatGroup group = pData->StatGroup;
        const EStatOp op = pData->StatOp;

        // ── EStat -> 데이터 매핑 ──
        if (stat == EStat::None)
        {
            LOG_WRITE(LogLevel::Error, std::format("Stat data has EStat::None. (Key={})", key));
            return false;
        }
        if (sm_dataByStatMap.contains(stat))
        {
            LOG_WRITE(LogLevel::Error, std::format("Duplicate EStat in Stat data. (Stat={})", static_cast<int>(stat)));
            return false;
        }
        sm_dataByStatMap.insert(std::pair(stat, pData));

        // ── 그룹/Op 유효성 ──
        const size_t statIdx = static_cast<size_t>(stat);
        const size_t groupIdx = static_cast<size_t>(group);
        if (group == EStatGroup::None || groupIdx >= sm_groupInfo.size())
        {
            LOG_WRITE(LogLevel::Error, std::format("Invalid StatGroup. (Stat={}, Group={})", static_cast<int>(stat), static_cast<int>(group)));
            return false;
        }
        if (op == EStatOp::None || static_cast<size_t>(op) >= static_cast<size_t>(EStatOp::Max))
        {
            LOG_WRITE(LogLevel::Error, std::format("Invalid StatOp. (Stat={}, Op={})", static_cast<int>(stat), static_cast<int>(op)));
            return false;
        }

        // ── EStat -> 그룹 ──
        sm_statToGroup[statIdx] = group;

        // ── 그룹 -> 구성 스탯 ──
        StatGroupInfo& info = sm_groupInfo[groupIdx];
        if (op == EStatOp::Total)
        {
            if (info.total != EStat::None)
            {
                LOG_WRITE(LogLevel::Error, std::format("Duplicate Total stat in group. (Group={})", static_cast<int>(group)));
                return false;
            }
            info.total = stat;
        }
        else
        {
            EStat& slot = info.slot[static_cast<size_t>(op)];
            if (slot != EStat::None)
            {
                LOG_WRITE(LogLevel::Error, std::format("Duplicate Op in group. (Group={}, Op={})", static_cast<int>(group), static_cast<int>(op)));
                return false;
            }
            slot = stat;
        }
    }

    return true;
}

const GameData_Stat* GameDataTable_Stat::FindDataByStat(EStat stat)
{
    auto iter = sm_dataByStatMap.find(stat);
    if (iter == sm_dataByStatMap.cend())
        return nullptr;
    return iter->second;
}

const StatGroupInfo* GameDataTable_Stat::GetGroupInfo(EStatGroup group)
{
    const size_t idx = static_cast<size_t>(group);
    if (group == EStatGroup::None || idx >= sm_groupInfo.size())
        return nullptr;
    return &sm_groupInfo[idx];
}

EStatGroup GameDataTable_Stat::GetStatGroup(EStat stat)
{
    const size_t idx = static_cast<size_t>(stat);
    if (idx >= sm_statToGroup.size())
        return EStatGroup::None;
    return sm_statToGroup[idx];
}
