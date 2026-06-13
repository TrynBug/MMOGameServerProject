#pragma once

#include "pch.h"
#include "Skills/EffectParams.h"

// 전방선언. Update 에서 포인터로만 사용하므로 완전타입은 StageSkill.cpp 에서 include.
class Stage;

// ─────────────────────────────────────────────────────────────
// MonsterProjectile — 서버 권위 몬스터 투사체 (스킬/효과 시스템)
// ─────────────────────────────────────────────────────────────
//
// 플레이어 투사체(ProjectileGroup, 클라가 hit 보고 → 서버 검증)와 달리, 몬스터 투사체는
// 피해자가 플레이어라 피해자 클라에 hit 판정을 맡길 수 없다(회피·치트 익스플로잇). 그래서
// 서버가 직접 매 tick 전진 + 충돌 판정한다. 클라는 SkillCastNtf 로 비주얼만 재현(보고 안 함).
//
// 매 tick: origin + dir*speed*t 로 전진 → 현재 위치 hit 반경(shape.radius) 안 적(진영 규칙: Monster→User)
//   검사 → 적중 시 ApplyEffectDamage (+OnHitSkillKey 폭발) 후 소멸. maxRange 도달 시 (폭발 후) 소멸.
//
// Stage 가 소유하며(updateSkillEffects 에서 매 tick Update), 컨텐츠 스레드에서만 접근한다. 락 없음.
// 시간은 절대 시계가 아니라 spawn 이후 누적 경과시간(deltaMs 누적)으로 다룬다.
class MonsterProjectile
{
public:
    explicit MonsterProjectile(const EffectParams& params) : m_params(params) {}

    MonsterProjectile(const MonsterProjectile&) = delete;
    MonsterProjectile& operator=(const MonsterProjectile&) = delete;

    // 매 tick(Stage 컨텐츠 스레드) 호출. 만료(적중/최대사거리 도달)되면 true 를 리턴한다.
    // 정의는 StageSkill.cpp (Stage 완전타입 + 효과 파이프라인 필요).
    bool Update(Stage* pStage, int64 deltaMs);

private:
    EffectParams m_params;
    int64        m_elapsedMs = 0;   // spawn 이후 누적 경과시간 (위치 계산 기준)
    bool         m_expired   = false;
};
