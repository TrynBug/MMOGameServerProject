#pragma once

#include "pch.h"

#include <vector>

class Stage;
class StageLayout;
struct GameData_Spawner;

// ─────────────────────────────────────────────────────────────
// MonsterSpawner
// ─────────────────────────────────────────────────────────────
//
// Stage 가 소유하는 몬스터 스폰 관리자.
// 레이아웃의 Spawner 배치(위치/반경) + GameData_Spawner 동작데이터(SpawnGroup/MaxPacks/
// 리스폰/활성화)를 결합하여, 팩(무리) 단위로 스폰/리스폰/밀도유지/활성화 게이팅을 한다.
//
// 스레드: 컨텐츠 스레드 전용(락 없음). Stage::OnUpdate 에서 매 tick Update 호출.
// 자세한 설계: 몬스터스폰.md 참조.
class MonsterSpawner
{
public:
    // 레이아웃 Spawner 목록을 순회하며 각 key 의 GameData_Spawner 동작데이터를 결합한다.
    void Load(Stage& stage, const StageLayout& layout);

    // 활성화 판정 + 전멸 폴링 + 리스폰 + 밀도 유지.
    void Update(int64 deltaMs);

    // Manual 활성화 스포너 on/off (스크립트 ActivateSpawner 연동용. v1 미사용).
    void Activate(int32 spawnerKey);
    void Deactivate(int32 spawnerKey);

private:
    // 한 번 배치된 무리 1개. 멤버는 objectId 로만 보관(생애주기 비결합).
    struct Pack
    {
        std::vector<int64> memberIds;
        int64 respawnTimerMs = 0;
        bool  pending        = false;   // 전멸 후 리스폰 대기중
    };

    // 활성 스포너 1개. 위치/반경은 레이아웃, 동작은 GameData_Spawner.
    struct ActiveSpawner
    {
        const GameData_Spawner* data = nullptr;
        float centerX = 0.f, centerY = 0.f, centerZ = 0.f;
        float radius  = 0.f;
        bool  manualOn   = false;   // Manual 활성화 상태
        bool  filledOnce = false;   // 리스폰 없는(던전) 스포너의 최초 채움 여부
        std::vector<Pack> packs;
    };

    bool  isActive(const ActiveSpawner& s) const;
    bool  isPlayerNear(const ActiveSpawner& s) const;
    int32 aliveCount(const Pack& pack) const;
    void  fillPack(ActiveSpawner& s, Pack& pack);   // SpawnGroup 읽어 멤버 스폰 + memberIds 채움

    Stage* m_pStage = nullptr;
    std::vector<ActiveSpawner> m_spawners;
};
