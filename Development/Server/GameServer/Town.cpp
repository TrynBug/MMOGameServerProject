#include "pch.h"
#include "Town.h"
#include "Character.h"     // GetCurrentCharacter가 CharacterPtr을 리턴하여 완전타입 필요
#include "GameServer.h"   // StageEnterNtf 전송을 위해 완전 타입 필요

Town::Town(int64 stageId)
    : Town(stageId, LoadStageGridParams(stageId))   // 위임. LoadStageGridParams는 1회만 호출.
{
}

Town::Town(int64 stageId, const StageGridParams& params)
    : Stage(stageId,
            params.stageType,
            params.worldMinX, params.worldMinY,
            params.worldMaxX, params.worldMaxY,
            params.sectorSize)
{
}

void Town::OnStageUpdate(int64 /*deltaMs*/)
{
    // 마을 Stage. 현재는 처리할 게 없음.
    // 향후 NPC 업데이트, 마을 이벤트 등이 들어갈 예정.
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

    pGameServer->SendStageEnterNtf(spUser->GetUserId(), GetStageId(),
        spCharacter->GetPosX(), spCharacter->GetPosY(), spCharacter->GetYaw());
}
