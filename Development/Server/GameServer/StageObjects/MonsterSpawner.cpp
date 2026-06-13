#include "pch.h"
#include "StageObjects/MonsterSpawner.h"

#include "Stages/Stage.h"
#include "Stages/StageLayout.h"
#include "StageObjects/Monster.h"
#include "Stages/Sector.h"

#include "Generated/GameData_Spawner.h"
#include "Generated/GameData_SpawnGroup.h"
#include "Enum/GameEnum_Monster.h"

#include <random>
#include <cmath>
#include <algorithm>

namespace
{
    // 컨텐츠 스레드별 독립 RNG (rand() 의 전역상태 경쟁 회피).
    float frand01()
    {
        static thread_local std::mt19937 rng(std::random_device{}());
        static thread_local std::uniform_real_distribution<float> dist(0.f, 1.f);
        return dist(rng);
    }
}

void MonsterSpawner::Load(Stage& stage, const StageLayout& layout)
{
    m_pStage = &stage;

    for (const auto& sp : layout.GetSpawners())
    {
        const GameData_Spawner* pData = GameDataTable_Spawner::FindData(sp.key);
        if (!pData)
        {
            LOG_WRITE(LogLevel::Warn, std::format("Spawner data not found. stageId={} spawnerKey={}", stage.GetStageId(), sp.key));
            continue;
        }

        ActiveSpawner as;
        as.data    = pData;
        as.centerX = sp.posX;
        as.centerY = sp.posY;
        as.centerZ = sp.posZ;
        as.radius  = sp.radius;
        m_spawners.push_back(std::move(as));
    }

    LOG_WRITE(LogLevel::Info, std::format("MonsterSpawner loaded. stageId={} spawners={}", stage.GetStageId(), m_spawners.size()));
}

void MonsterSpawner::Update(int64 deltaMs)
{
    for (auto& s : m_spawners)
    {
        if (!isActive(s))
            continue;   // dormant: 기존 몬스터는 그대로 두고 밀도유지/리스폰만 정지

        const bool respawning = (s.data->RespawnDelayMs >= 0);

        // 1. 리스폰 타이머 진행 → 만료 시 재배치
        for (auto& pack : s.packs)
        {
            if (pack.pending)
            {
                pack.respawnTimerMs -= deltaMs;
                if (pack.respawnTimerMs <= 0)
                {
                    fillPack(s, pack);
                    pack.pending = false;
                }
            }
        }

        // 2. 전멸 감지 (멤버 전원 디스폰됨)
        for (auto it = s.packs.begin(); it != s.packs.end(); )
        {
            if (!it->pending && aliveCount(*it) == 0)
            {
                if (respawning)
                {
                    it->pending        = true;
                    it->respawnTimerMs = s.data->RespawnDelayMs;
                    it->memberIds.clear();
                    ++it;
                }
                else
                {
                    it = s.packs.erase(it);   // 던전: 리스폰 없음
                    continue;
                }
            }
            else
            {
                ++it;
            }
        }

        // 3. 밀도 유지
        if (respawning)
        {
            while (static_cast<int32>(s.packs.size()) < s.data->MaxPacks)
            {
                Pack pack;
                fillPack(s, pack);
                s.packs.push_back(std::move(pack));
            }
        }
        else if (!s.filledOnce)
        {
            for (int32 i = 0; i < s.data->MaxPacks; ++i)
            {
                Pack pack;
                fillPack(s, pack);
                s.packs.push_back(std::move(pack));
            }
            s.filledOnce = true;
        }
    }
}

bool MonsterSpawner::isActive(const ActiveSpawner& s) const
{
    switch (s.data->Activation)
    {
    case ESpawnActivation::Always:          return true;
    case ESpawnActivation::PlayerProximity: return isPlayerNear(s);
    case ESpawnActivation::Manual:          return s.manualOn;
    default:                                return false;
    }
}

bool MonsterSpawner::isPlayerNear(const ActiveSpawner& s) const
{
    int32 cx = 0, cz = 0;
    if (!m_pStage->GetSectorIndex(s.centerX, s.centerZ, cx, cz))
        return false;

    const double sectorSize = m_pStage->GetSectorSize();
    int32 range = 1;
    if (sectorSize > 0.0)
    {
        const int32 r = static_cast<int32>(std::ceil(s.data->ActivationRange / sectorSize));
        if (r > range)
            range = r;
    }

    bool found = false;
    m_pStage->ForEachAdjacentSector(cx, cz, range,
        [&](Sector* pSector)
        {
            if (!pSector->GetUsers().empty())
                found = true;
        });
    return found;
}

int32 MonsterSpawner::aliveCount(const Pack& pack) const
{
    int32 n = 0;
    for (int64 id : pack.memberIds)
        if (m_pStage->FindObject(id) != nullptr)
            ++n;
    return n;
}

void MonsterSpawner::fillPack(ActiveSpawner& s, Pack& pack)
{
    pack.memberIds.clear();

    const GameData_SpawnGroup* pGroup = GameDataTable_SpawnGroup::FindData(s.data->SpawnGroupKey);
    if (!pGroup)
    {
        LOG_WRITE(LogLevel::Warn, std::format("SpawnGroup not found. spawnGroupKey={}", s.data->SpawnGroupKey));
        return;
    }

    // 팩 기준 위치: 밀도존이면 NavMesh 랜덤포인트, 앵커(radius 0)면 center.
    float leaderX = s.centerX, leaderY = s.centerY, leaderZ = s.centerZ;
    if (s.radius > 0.f)
    {
        float rx = 0.f, ry = 0.f, rz = 0.f;
        if (m_pStage->SampleRandomNavPoint(s.centerX, s.centerY, s.centerZ, s.radius, rx, ry, rz))
        {
            leaderX = rx;
            leaderY = ry;
            leaderZ = rz;
        }
    }

    const float scatter = pGroup->ScatterRadius;
    for (int32 gi = 0; gi < pGroup->GetMonsterKeyCount(); ++gi)
    {
        const int32 monsterKey = pGroup->GetMonsterKey(gi);
        const int32 count      = pGroup->GetMonsterCount(gi);
        if (monsterKey == 0 || count <= 0)
            continue;

        for (int32 c = 0; c < count; ++c)
        {
            float px = leaderX;
            float pz = leaderZ;
            if (scatter > 0.f)
            {
                const float ang  = frand01() * 6.2831853f;
                const float dist = scatter * std::sqrt(frand01());   // 균일 분포
                px += std::cos(ang) * dist;
                pz += std::sin(ang) * dist;
            }

            // SpawnMonster 가 NavMesh 스냅 + 월드경계 검증 + 거부처리를 담당(좌표만 넘긴다).
            Monster* pMonster = m_pStage->SpawnMonster(monsterKey, px, leaderY, pz, 0.f);
            if (pMonster)
            {
                pMonster->SetSpawnerKey(s.data->Key);   // 스포너별 사망 콜백(OnSpawnerMonsterDead) 용 태깅
                pack.memberIds.push_back(pMonster->GetObjectId());
            }
        }
    }
}

void MonsterSpawner::Activate(int32 spawnerKey)
{
    for (auto& s : m_spawners)
        if (s.data->Key == spawnerKey)
            s.manualOn = true;
}

void MonsterSpawner::Deactivate(int32 spawnerKey)
{
    for (auto& s : m_spawners)
        if (s.data->Key == spawnerKey)
            s.manualOn = false;
}
