#include "pch.h"
#include "Town.h"

Town::Town(int64 stageId, int64 stageDataKey)
    : Town(stageId, stageDataKey, LoadStageGridParams(stageDataKey))   // 위임. LoadStageGridParams는 1회만 호출.
{
}

Town::Town(int64 stageId, int64 stageDataKey, const StageGridParams& params)
    : Stage(stageId, stageDataKey,
            params.stageType,
            params.worldMinX, params.worldMinZ,
            params.worldMaxX, params.worldMaxZ,
            params.sectorSize)
{
}

void Town::OnStageUpdate(int64 /*deltaMs*/)
{
    // 마을 Stage. 현재는 처리할 게 없음.
    // 향후 NPC 업데이트, 마을 이벤트 등이 들어갈 예정.
}

void Town::OnStart()
{
    // 부모 처리 (로그 등).
    Stage::OnStart();

    // [임시 테스트] 마을 시작 시 몬스터 1마리 하드코딩 스폰.
    //  - 서버 내부(m_objects/m_monsterObjects + sector)에만 등록된다. 클라 가시성 전파는 아직 없음.
    //  - 좌표는 맵 중앙(항상 world bounds 내부 → sector 등록 보장). monsterKey 50 은 Monster.csv 첫 데이터.
    //  - 추후 스폰 데이터 테이블/스포너 시스템으로 대체 예정.
    for (int i = 0; i < 10; ++i)
    {
        SpawnMonster(50, i*1.f, 0, 0, 0.0f);
    }

    for(int i=0; i<10; ++i)
        SpawnMonster(60, i * 1.f, 0, 30, 0.0f);
}

// 유저 입장/캐릭터 스폰은 Stage 베이스의 2단계 입장 흐름이 처리한다
// (OnUserEnter → 클라 StageLoadCompleteReq → spawnPendingCharacter → StageLoadCompleteRes).
