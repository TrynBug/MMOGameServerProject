#pragma once

#include "pch.h"

#include <vector>

// ─────────────────────────────────────────────────────────────
// StageLayout
// ─────────────────────────────────────────────────────────────
//
// 유니티 에디터에서 Stage 위에 배치한 오브젝트(스포너/이벤트영역 등)를 export 한
// 배치데이터(Map/StageLayout/<stageDataKey>.json)를 로드한다.
// 스크립트/스포너는 좌표를 손입력하지 않고 이 배치를 Key 로 참조한다.
//
// v1: Spawner 배치(key+pos+radius)만 로드한다. (SpawnPoint/EventArea 등은 스크립트 단계에서 확장.)
//
// 스레드: Stage 당 1개, 컨텐츠 스레드 전용(락 없음).
class StageLayout
{
public:
    struct SpawnerPlacement
    {
        int32 key    = 0;
        float posX   = 0.f;
        float posY   = 0.f;
        float posZ   = 0.f;
        float radius = 0.f;   // 0 = 고정앵커, > 0 = 밀도존
    };

    // stageDataKey 의 레이아웃 파일을 로드한다.
    // 파일이 없으면 빈 레이아웃으로 두고 true 를 반환한다(레이아웃 없는 Stage 는 정상).
    // 파일이 있으나 파싱 실패하면 false.
    bool Load(int32 stageDataKey);

    const std::vector<SpawnerPlacement>& GetSpawners() const { return m_spawners; }

private:
    std::vector<SpawnerPlacement> m_spawners;
};
