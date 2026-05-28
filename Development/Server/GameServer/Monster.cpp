#include "pch.h"
#include "Monster.h"

Monster::Monster(int64 objectId, const GameData_Monster* pMonsterData)
    : ActorObject(objectId, EObjectType::Monster)
    , m_pMonsterData(pMonsterData)
{
    // 종류 데이터의 기본스탯을 스탯 컴포넌트에 적용.
    applyBaseStats();

    // 현재 HP/MP 를 최대치로 초기화 (스폰 시 풀피).
    // applyBaseStats() 이후여야 HpTotal/MpTotal 이 계산되어 있어 최대치가 정해진다.
    FillHp();
    FillMp();
}

void Monster::applyBaseStats()
{
    if (m_pMonsterData == nullptr)
    {
        LOG_WRITE(LogLevel::Error, std::format("Monster::applyBaseStats - monster data is null. objectId={}", GetObjectId()));
        return;
    }

    // (EStat, value) 쌍 목록을 순회하며 적용. Stat 이 None 인 슬롯은 건너뛴다.
    // (JobBase 와 동일 패턴. 3번째 소비처가 생기면 템플릿 헬퍼로 통합 예정.)
    const int32 statCount = m_pMonsterData->GetStatCount();
    for (int32 i = 0; i < statCount; ++i)
    {
        const EStat stat = m_pMonsterData->GetStat(i);
        if (stat == EStat::None)
            continue;
        m_statComponent.ApplyStat(stat, m_pMonsterData->GetStatValue(i));
    }
}
