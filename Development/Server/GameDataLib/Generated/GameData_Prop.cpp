// =====================================================================
// 이 파일은 GameDataGenerator 에 의해 최초 1회만 생성됩니다.
// 이후에는 사용자가 직접 수정할 수 있습니다.
// =====================================================================

#include "LoggerLib.h"
#include "GameData_Prop.h"


bool GameData_Prop::Initialize()
{
    // 데이터 1건을 읽은 직후 호출됨

    return true;
}

bool GameDataTable_Prop::OnAddData(const GameData* pRawData)
{
    const GameData_Prop* pData = static_cast<const GameData_Prop*>(pRawData);

    // 데이터가 sm_dataMap 에 추가된 후 호출됨

    return true;
}

bool GameDataTable_Prop::OnLoadComplete()
{
    for (const auto& [key, pData] : sm_dataMap)
    {
        // 여기에 전체 데이터 로드 완료 후 사용자 로직을 작성합니다.
        // 예: 다른 테이블과의 참조 연결, 유효성 검증 등
    }

    return true;
}
