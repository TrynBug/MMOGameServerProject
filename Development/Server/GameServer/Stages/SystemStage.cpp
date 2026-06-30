#include "pch.h"
#include "Stages/SystemStage.h"
#include "GameServer.h"

SystemStage::SystemStage(int64 stageId, int32 stageDataKey)
    : SystemStage(stageId, stageDataKey, LoadStageGridParams(stageDataKey))   // 위임. LoadStageGridParams는 1회만 호출.
{
}

SystemStage::SystemStage(int64 stageId, int32 stageDataKey, const StageGridParams& params)
    : Stage(stageId, stageDataKey,
            params.stageType,
            params.worldMinX, params.worldMinZ,
            params.worldMaxX, params.worldMaxZ,
            params.sectorSize)
{
}

void SystemStage::OnStageUpdate(int64 /*deltaMs*/)
{
    // 캐릭터 선택창 Stage. 현재는 처리할 게 없음.
    // 향후 캐릭터 선택 패킷 핸들러 등이 들어갈 예정.
}

void SystemStage::OnUserEnter(const UserPtr& spUser)
{
    // 공통 입장 처리(m_users 등록, currentStageId 설정 등)는 베이스가 담당.
    Stage::OnUserEnter(spUser);
    if (!spUser)
        return;

    // 캐릭터 선택창에 들어온 시점 = 캐릭터 목록을 보낼 유일한 시점.
    // 로그인 최초 입장(handleGatewayUserEnter)과 캐릭터선택 복귀(handleReturnToCharacterSelectReq)가 여기로 모인다.
    //
    // 이 Stage 의 resume executor 를 넘겨, DB await 후속작업(전송)이 이 SystemStage 의 컨텐츠 스레드에서 재개되게 한다(코루틴 후속로직의 스레드 일관성).
    // SystemStage 는 영속 Stage 라 파괴/이동 걱정이 없어 AsyncPin 은 불필요하다.
    GameServer::Instance().SendCharacterListForUser(spUser->GetAccountId(), spUser->GetGameDbIndex(), GetResumeExecutor());
}
