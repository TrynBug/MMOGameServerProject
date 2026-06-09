// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 최초 1회만 생성됩니다.
// 이후에는 사용자가 직접 수정할 수 있습니다.
// =====================================================================

#include "LoggerLib.h"
#include "GameData_StageStartPosition.h"
#include "GameData_Stage.h"   // OnLoadComplete 검증: 이동 가능한 Stage(Town/Field)의 Default 행 필수


bool GameData_StageStartPosition::Initialize()
{
    // 데이터 1건을 읽은 직후 호출됨

    return true;
}

bool GameDataTable_StageStartPosition::OnAddData(const GameData* pRawData)
{
    const GameData_StageStartPosition* pData = static_cast<const GameData_StageStartPosition*>(pRawData);

    // (StageKey, StagePositionType) 조합 유일 보장 + 조회 인덱스 구축.
    const auto key = std::make_pair(pData->StageKey, pData->StagePositionType);
    const auto [iter, inserted] = sm_byStageAndType.emplace(key, pData);
    if (!inserted)
    {
        LOG_WRITE(LogLevel::Error, std::format(
            "duplicate (StageKey, StagePositionType). Key={} StageKey={} positionType={}",
            pData->Key, pData->StageKey, static_cast<int>(pData->StagePositionType)));
        return false;
    }

    return true;
}

bool GameDataTable_StageStartPosition::OnLoadComplete()
{
    // 검증 1: 모든 행의 StageKey 가 실제 Stage 데이터를 가리키는지.
    for (const auto& [key, pData] : sm_dataMap)
    {
        if (!GameDataTable_Stage::FindData(pData->StageKey))
        {
            LOG_WRITE(LogLevel::Error, std::format( "unknown StageKey. Key={} StageKey={}", pData->Key, pData->StageKey));
            return false;
        }
    }

    // 검증 2: 이동 가능한 Stage(Town/Field)는 Default 시작위치가 반드시 있어야 한다.
    for (const auto& [stageKey, pStageData] : GameDataTable_Stage::GetDataMap())
    {
        if (pStageData->StageType != EStageType::Town && pStageData->StageType != EStageType::Field)
            continue;

        if (!FindByStageAndType(stageKey, EStagePositionType::Default))
        {
            LOG_WRITE(LogLevel::Error, std::format( "missing Default position. StageKey={}", stageKey));
            return false;
        }
    }

    return true;
}

const GameData_StageStartPosition* GameDataTable_StageStartPosition::FindByStageAndType(int64_t stageKey, EStagePositionType positionType)
{
    const auto iter = sm_byStageAndType.find(std::make_pair(stageKey, positionType));
    if (iter == sm_byStageAndType.end())
        return nullptr;
    return iter->second;
}
