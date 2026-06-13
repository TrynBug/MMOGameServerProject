#pragma once

#include "pch.h"

#include <vector>
#include <array>
#include <string>

// ─────────────────────────────────────────────────────────────
// StageLayout
// ─────────────────────────────────────────────────────────────
//
// 유니티 에디터에서 Stage 위에 배치한 오브젝트(스포너/스폰지점/경로 등)를 export 한
// 배치데이터(Map/StageLayout/<StageLayoutFileName>.json — GameData_Stage.StageLayoutFileName, 예: Town.json)를 로드한다.
// 스크립트/스포너는 좌표를 손입력하지 않고 이 배치를 Key 로 참조한다.
//
// v1: Spawner(key+pos+radius) + SpawnPoint(key+pos+yaw) + Waypoint(key+points)
//     + EventArea(key+shape+center+크기) 로드.
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

    // 이벤트영역 배치데이터(에디터 export). 런타임 상태/판정은 EventArea(StageObject 파생)가 갖는다.
    // 모양 판정은 평면(X-Z)으로만 한다(쿼터뷰·NavMesh 게임이라 높이 무시).
    struct EventArea
    {
        int32 key    = 0;
        int32 shape  = 0;     // 0 = Sphere(원), 1 = Box(AABB)
        float cx     = 0.f;   // center
        float cy     = 0.f;
        float cz     = 0.f;
        float radius = 0.f;   // Sphere 반경
        float sizeX  = 0.f;   // Box 전체 크기(extent)
        float sizeZ  = 0.f;
        bool  secure = false; // true = 클라 미신뢰. 서버가 매 tick 권위 위치로 직접 폴링.
    };

    // 상호작용 오브젝트(문/레버/NPC 등) 배치 데이터. 클라가 prop 근처에서 상호작용 시 서버가
    // 권위 위치 ↔ 이 위치의 거리(range 내)를 검증한 뒤 OnObjectInteract 발동(Stage스크립트.md §6·§7.3).
    struct Prop
    {
        int32 key   = 0;
        int32 type  = 0;     // 용도 구분(0=기본/문/레버/NPC… 스크립트가 해석). v1은 참고값.
        float x     = 0.f;
        float y     = 0.f;
        float z     = 0.f;
        float range = 2.f;   // 상호작용 허용 반경(m, 평면). 서버 검증 기준.
    };

    // stageLayoutFileName 의 레이아웃 파일(<stageLayoutFileName>.json)을 로드한다.
    // 이름이 비면 빈 레이아웃으로 두고 true(레이아웃 없는 Stage 는 정상).
    // 이름이 명시됐는데 파일이 없거나 파싱 실패하면 false (fail-fast).
    bool Load(const std::string& stageLayoutFileName);

    const std::vector<SpawnerPlacement>& GetSpawners()   const { return m_spawners; }
    const std::vector<EventArea>&        GetEventAreas() const { return m_eventAreas; }

    // Key 로 조회. 없으면 nullptr.
    const SpawnPoint* GetSpawnPoint(int32 key) const;
    const Waypoint*   GetWaypoint(int32 key) const;
    const EventArea*  GetEventArea(int32 key) const;
    const Prop*       GetProp(int32 key) const;

private:
    std::vector<SpawnerPlacement> m_spawners;
    std::vector<SpawnPoint>       m_spawnPoints;
    std::vector<Waypoint>         m_waypoints;
    std::vector<EventArea>        m_eventAreas;
    std::vector<Prop>             m_props;
};
