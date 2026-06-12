#pragma once

#include "pch.h"

#include <vector>
#include <array>

// ─────────────────────────────────────────────────────────────
// StageLayout
// ─────────────────────────────────────────────────────────────
//
// 유니티 에디터에서 Stage 위에 배치한 오브젝트(스포너/스폰지점/경로 등)를 export 한
// 배치데이터(Map/StageLayout/<stageDataKey>.json)를 로드한다.
// 스크립트/스포너는 좌표를 손입력하지 않고 이 배치를 Key 로 참조한다.
//
// v1: Spawner(key+pos+radius) + SpawnPoint(key+pos+yaw) + Waypoint(key+points) 로드.
//     (EventArea 등은 후속 단계에서 확장.)
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

    struct SpawnPoint
    {
        int32 key  = 0;
        float posX = 0.f;
        float posY = 0.f;
        float posZ = 0.f;
        float yaw  = 0.f;
    };

    struct Waypoint
    {
        int32 key = 0;
        std::vector<std::array<float, 3>> points;   // (x, y, z) 순서
    };

    // stageDataKey 의 레이아웃 파일을 로드한다.
    // 파일이 없으면 빈 레이아웃으로 두고 true 를 반환한다(레이아웃 없는 Stage 는 정상).
    // 파일이 있으나 파싱 실패하면 false.
    bool Load(int32 stageDataKey);

    const std::vector<SpawnerPlacement>& GetSpawners() const { return m_spawners; }

    // Key 로 조회. 없으면 nullptr.
    const SpawnPoint* GetSpawnPoint(int32 key) const;
    const Waypoint*   GetWaypoint(int32 key) const;

private:
    std::vector<SpawnerPlacement> m_spawners;
    std::vector<SpawnPoint>       m_spawnPoints;
    std::vector<Waypoint>         m_waypoints;
};
