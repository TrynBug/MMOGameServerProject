#include "pch.h"
#include "Town.h"
#include "Character.h"     // GetCurrentCharacter가 CharacterPtr을 리턴하여 완전타입 필요
#include "GameServer.h"   // StageEnterNtf 전송을 위해 완전 타입 필요

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

void Town::OnUserEnter(const UserPtr& spUser, const CharacterPtr& spCharacter)
{
    // 부모 처리 (User를 m_users에, Character를 m_objects/m_userObjects에 등록).
    Stage::OnUserEnter(spUser, spCharacter);

    // 클라이언트에게 StageEnterNtf 전송.
    GameServer* pGameServer = GetGameServer();
    if (!pGameServer)
    {
        LOG_WRITE(LogLevel::Error, std::format("Town::OnUserEnter - GameServer not injected. stageId={} userId={}",
            GetStageId(), spUser->GetUserId()));
        return;
    }

    if (!spCharacter)
    {
        LOG_WRITE(LogLevel::Error, std::format("Town::OnUserEnter - no character. stageId={} userId={}",
            GetStageId(), spUser->GetUserId()));
        return;
    }

	pGameServer->SendStageEnterNtf(spUser->GetUserId(), GetStageId(), GetStageDataKey(),
        spCharacter->GetPosX(), spCharacter->GetPosY(), spCharacter->GetPosZ(), spCharacter->GetYaw());
}
