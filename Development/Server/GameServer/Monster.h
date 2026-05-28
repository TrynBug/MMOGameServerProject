#pragma once

#include "pch.h"
#include "ActorObject.h"
#include "BasicStatComponent.h"

#include "Enum/GameEnum_Stat.h"             // EStatGroup
#include "Generated/GameData_Monster.h"     // GameData_Monster

// ─────────────────────────────────────────────────────────────
// Monster 클래스
// ─────────────────────────────────────────────────────────────
//
// 게임 런타임에 스폰되는 몬스터. ActorObject 를 상속받는다.
// 캐릭터와 달리 DB 에 저장되지 않으며, 몬스터 종류 게임데이터(GameData_Monster)로부터 생성된다.
//
// ── 스탯 ──
// 경량 스탯 컴포넌트(BasicStatComponent)를 사용한다.
// 생성 시 종류 데이터의 (Stat#, StatValue#) 목록을 순회하며 ApplyStat 으로 적용한다.
//
// ── 라이프타임 ──
// Stage 가 강한 소유자(shared_ptr). 스폰 시 ObjectId 는 외부에서 발급해 전달한다.
//
// ── 좌표계 ──
// Unity 와 동일. (X, Y, Z) 3D. Y는 높이, X-Z 가 평면. yaw 는 Y축 회전, degree.
//
// 현재 단계 범위: 스탯 + 현재HP/MP + 종류데이터 참조까지.
// AI, 이동, 타겟, 어그로 등은 이후 단계에서 추가한다.
class Monster : public ActorObject
{
public:
    // objectId 는 외부(스폰 로직)에서 ObjectIdGenerator 로 발급해 전달한다.
    // pMonsterData 는 몬스터 종류 데이터(스탯/등급/경험치 등). 호출자가 유효성을 보장한다.
    // 위치는 생성 후 SetPos/SetYaw 로 설정한다.
    Monster(int64 objectId, const GameData_Monster* pMonsterData);
    ~Monster() override = default;

    Monster(const Monster&) = delete;
    Monster& operator=(const Monster&) = delete;

public:
    // ── 종류 데이터 접근 ───────────────────────────────────────
    const GameData_Monster* GetMonsterData() const { return m_pMonsterData; }

    // ── 스탯 ──────────────────────────────────────────────────
    BasicStatComponent&       GetStat()       { return m_statComponent; }
    const BasicStatComponent& GetStat() const { return m_statComponent; }

    // ── 총합 스탯 읽기 (ActorObject 오버라이드) ──────────────
    // 경량 컴포넌트는 그룹 인덱스 배열에 총합을 보관하므로 바로 GetTotal.
    // GetMaxHp/GetMaxMp 는 ActorObject 가 이 함수 위에서 공통 구현한다.
    double GetStatTotal(EStatGroup group) const override { return m_statComponent.GetTotal(group); }

private:
    // 생성자에서 호출. 종류 데이터의 기본스탯을 m_statComponent 에 적용한다.
    void applyBaseStats();

    // 몬스터 종류 데이터 (소유권 없음, 게임데이터는 로드 후 불변).
    const GameData_Monster* m_pMonsterData = nullptr;

    // 경량 스탯 컴포넌트. 생성자에서 종류 데이터의 기본스탯을 적용한다.
    BasicStatComponent m_statComponent;
};

using MonsterPtr  = std::shared_ptr<Monster>;
using MonsterWPtr = std::weak_ptr<Monster>;
