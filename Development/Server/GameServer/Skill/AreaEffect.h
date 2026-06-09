#pragma once

#include "pch.h"
#include "Skill/EffectParams.h"

// 전방선언. Update 에서 포인터로만 사용하므로 완전타입은 AreaEffect.cpp 에서 include.
class Stage;

// ─────────────────────────────────────────────────────────────
// AreaEffect — 범위 대미지 효과 (스킬/효과 시스템)
// ─────────────────────────────────────────────────────────────
//
// "특정 시각에, 컨텐츠 스레드에서, 범위 쿼리 1회 + 대미지" 라는 단일 동작을 횟수만 달리하여
// 통합 표현한다:
//   - 단일 틱(tickIntervalMs <= 0)  : instant. 전격방출(0.3s 지연), 메테오 착탄(1s 지연), 글라이드 충격파 등.
//   - 다중 틱(tickIntervalMs >  0)  : periodic. 얼음지대, 화염지대.
//   - Linear 모션                    : 이동하는 장판(하이브리드).
//
// Stage 가 소유하며(컨테이너 + updateSkillEffects), 컨텐츠 스레드에서만 접근한다. 별도 락 없음.
// 가시성(늦게 진입한 관전자)은 v1 에서 다루지 않는다 — 시전 시점 AOI 통보로 충분히 본다고 가정.
//
// 시간은 절대 시계가 아니라 spawn 이후 누적 경과시간(deltaMs 누적)으로 다룬다 (BuffComponent 와 동일 방식).
class AreaEffect
{
public:
    explicit AreaEffect(const EffectParams& params);

    AreaEffect(const AreaEffect&) = delete;
    AreaEffect& operator=(const AreaEffect&) = delete;

    // 매 tick(Stage 컨텐츠 스레드) 호출. 도래한 틱을 모두 발사하고, 만료되면 true 를 리턴한다.
    // deltaMs 는 마지막 Update 이후 누적 경과시간.
    bool Update(Stage* pStage, int64 deltaMs);

private:
    // 1회 틱: 현재 위치를 계산하여 범위 안 적에게 대미지를 적용한다.
    void fireTick(Stage* pStage);

private:
    EffectParams m_params;

    int64 m_elapsedMs    = 0;   // spawn 이후 누적 경과시간 (위치 계산 / 틱 스케줄 기준)
    int64 m_nextTickAtMs = 0;   // 다음 틱 발사 시각 (m_elapsedMs 기준). 초기값 = firstTickDelayMs
    bool  m_expired      = false;
};
