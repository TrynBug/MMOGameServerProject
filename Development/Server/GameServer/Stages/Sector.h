#pragma once

#include "pch.h"

// 전방선언
class StageObject;

// ─────────────────────────────────────────────────────────────
// Sector 클래스
// ─────────────────────────────────────────────────────────────
//
// Stage를 격자로 분할한 1개의 칸을 표현한다.
// AOI(시야 범위) 계산, 브로드캐스팅 범위 제한 등 공간 분할 자료구조 용도.
//
// - 소유는 하지 않는다. raw pointer만 보관.
//   (객체 lifetime은 Stage가 shared_ptr로 소유)
// - 타입별로 분리된 컨테이너로 보관하여, 게임 로직에서
//   "이 섹터의 유저만", "이 섹터의 몬스터만" 같은 조회를 빠르게 한다.
// - 소속 Stage의 컨텐츠 스레드에서만 접근되므로 락 없음.
class Sector
{
public:
    Sector() = default;
    ~Sector() = default;

    Sector(const Sector&) = delete;
    Sector& operator=(const Sector&) = delete;

    // 이동은 허용 (std::vector<Sector>::resize 등에서 필요)
    Sector(Sector&&) noexcept = default;
    Sector& operator=(Sector&&) noexcept = default;

    // 섹터 좌표 (X-Z 평면)
    int32 GetSectorX() const { return m_sectorX; }
    int32 GetSectorZ() const { return m_sectorZ; }
    void  SetIndex(int32 sectorX, int32 sectorZ) { m_sectorX = sectorX; m_sectorZ = sectorZ; }

    // ── 객체 추가/제거 ──
    // 타입별 컨테이너에 추가/제거. 객체 타입은 호출자가 알고 있는 상태로 호출하기보다,
    // pObject->GetObjectType()로 자동 분기한다.
    void AddObject(StageObject* pObject);
    void RemoveObject(StageObject* pObject);

    // ── 타입별 조회 ──
    const std::unordered_map<int64, StageObject*>& GetUsers()    const { return m_users; }
    const std::unordered_map<int64, StageObject*>& GetMonsters() const { return m_monsters; }
    const std::unordered_map<int64, StageObject*>& GetProps()    const { return m_props; }
    const std::unordered_map<int64, StageObject*>& GetDrops()    const { return m_drops; }

    // 총 객체 수 (디버그/로그용)
    size_t GetTotalCount() const { return m_users.size() + m_monsters.size() + m_props.size() + m_drops.size(); }

private:
    int32 m_sectorX = -1;
    int32 m_sectorZ = -1;

    // 타입별 객체 컨테이너 (key=ObjectId, value=StageObject* raw pointer).
    // EObjectType이 늘어나면 여기에 컨테이너 추가 + Add/RemoveObject 분기 추가.
    std::unordered_map<int64, StageObject*> m_users;
    std::unordered_map<int64, StageObject*> m_monsters;
    std::unordered_map<int64, StageObject*> m_props;
    std::unordered_map<int64, StageObject*> m_drops;
};
