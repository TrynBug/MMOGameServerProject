#pragma once

#include "pch.h"
#include "GameServerDefine.h"

#include <unordered_map>
#include <string_view>

// Unity 프리팹 Root 직속 SkillCastOrigin을 export한 Map/CastAnchors.json을 읽는다.
// 서버는 프리팹을 직접 알지 못하므로, 여기의 Root 기준 local offset을 전투 판정에 사용한다.
class CastAnchorRegistry
{
public:
    // 파일이 아직 없으면 기존 중심 발사를 유지한다. 파싱 오류는 서버 기동 실패로 취급한다.
    bool Load();

    Vector3 GetPlayerLocalOffset(int32 jobId, int32 presetId) const;
    Vector3 GetMonsterLocalOffset(std::string_view prefabPath) const;

private:
    static uint64 makePlayerKey(int32 jobId, int32 presetId);

    std::unordered_map<uint64, Vector3> m_playerOffsets;
    std::unordered_map<std::string, Vector3> m_prefabOffsets;
};
