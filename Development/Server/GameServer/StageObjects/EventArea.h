#pragma once

#include "pch.h"
#include "StageObjects/StageObject.h"
#include "Stages/StageLayout.h"   // StageLayout::EventArea (배치데이터)

#include <unordered_set>

// ─────────────────────────────────────────────────────────────
// EventArea
// ─────────────────────────────────────────────────────────────
//
// Stage 위의 이벤트영역(함정/트리거/컷신존 등). StageObject 파생(EObjectType::EventArea).
//
// 트리거 방식 — 클라 선판정 + 서버 권위검증:
//   클라가 자기 캐릭터의 진입/이탈을 먼저 보고하면, 서버가 권위 위치를 이 영역의 Contains 로
//   검증한 뒤에만 스크립트 콜백(OnEnterEventArea/OnExitEventArea)을 발동한다.
//
// 가시성: 클라는 유니티 씬에서 영역을 이미 알고 있어 AOI 통보를 하지 않는다
//         (sector 등록·spawn/despawn Ntf 경로를 타지 않는다).
// 지오메트리: 중심 = StageObject 위치, 모양/크기는 자체 멤버. 평면(X-Z) 판정.
//
// 스레드: 소속 Stage 의 컨텐츠 스레드 전용(락 없음).
class EventArea : public StageObject
{
public:
    // 배치데이터로 초기화. objectId 는 서버 발급(내부 핸들 — 네트워크 비노출).
    [[nodiscard]] bool Initialize(int64 objectId, const StageLayout::EventArea& placement);

    int32 GetEventKey() const { return m_eventKey; }

    // true = 클라 미신뢰 영역. Stage 가 매 tick 권위 위치로 직접 폴링하고 클라 보고는 무시한다.
    bool IsSecure() const { return m_secure; }

    // (px, pz)가 영역 안인지 평면 판정. tolerance 만큼 경계를 넓혀 허용오차를 준다.
    bool Contains(float px, float pz, float tolerance) const;

    // occupant(현재 영역 안에 있는 userId) 관리 — 진입/이탈 중복방지·이탈판정용.
    bool AddOccupant(int64 userId)    { return m_occupants.insert(userId).second; }   // 신규 진입이면 true
    bool RemoveOccupant(int64 userId) { return m_occupants.erase(userId) != 0; }      // 안에 있었으면 true
    const std::unordered_set<int64>& GetOccupants() const { return m_occupants; }

private:
    int32 m_eventKey = 0;
    int32 m_shape    = 0;     // 0 = Sphere, 1 = Box
    float m_radius   = 0.f;   // Sphere 반경
    float m_sizeX    = 0.f;   // Box 전체 크기(extent)
    float m_sizeZ    = 0.f;
    bool  m_secure   = false;
    std::unordered_set<int64> m_occupants;
};
