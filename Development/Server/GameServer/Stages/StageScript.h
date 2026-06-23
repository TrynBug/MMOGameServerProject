#pragma once

#include "pch.h"

#include <memory>
#include <vector>
#include <string>

class Stage;

// ─────────────────────────────────────────────────────────────
// StageScript
// ─────────────────────────────────────────────────────────────
//
// Stage 당 1개의 Lua VM + 여러 스크립트(환경별 격리). 기획자가 작성한 Stage 로직을 로드/실행한다.
//
// 스레드: Stage 당 lua_State 1개 = 컨텐츠 스레드 1개 → 락 없음.
// 안전: 모든 Lua 진입은 pcall 격리 + lua_sethook 인스트럭션 상한(무한루프 차단).
//
// 구현은 pimpl 로 sol/Lua 타입을 헤더에서 완전히 숨긴다(무거운 sol 헤더 전파 방지 +
// sol::environment 가 타입 별칭이라 전방선언 불가한 문제 회피).
//
// P1(현재): VM + 다중 스크립트 환경 + 생애주기 콜백 멀티캐스트 + 가드 + 관측용 Log API.
//           (타이머/코루틴/이벤트영역/스폰 API 는 후속 단계.)
// 자세한 설계: Stage스크립트.md 참조.
class StageScript
{
public:
    StageScript();
    ~StageScript();

    StageScript(const StageScript&) = delete;
    StageScript& operator=(const StageScript&) = delete;

    // 각 스크립트를 개별 환경(_ENV)으로 로드한다. scriptNames = Map/StageScript/<name>.lua (확장자 제외).
    // 전역(_G)은 공유하되 콜백/지역상태는 환경에 격리된다(스크립트 간 충돌 없음).
    bool Load(Stage& stage, const std::vector<std::string>& scriptNames);

    // 생애주기 콜백 — 정의한 모든 스크립트에 멀티캐스트 (전부 pcall 격리 + 인스트럭션 가드).
    void CallOnStageStart();
    // 캐릭터가 Stage 에 스폰 완료된 뒤 호출(세션 입장 시점 아님). objectId(=characterId)를 넘긴다.
    void CallOnPlayerEnter(int64 objectId);
    // 캐릭터 퇴장 시 호출. 스폰됐던 캐릭터의 objectId. 캐릭터 없이 떠난 경우(미스폰 등)는 0.
    void CallOnPlayerLeave(int64 objectId);

    // 이벤트영역 진입/이탈 — Stage 가 클라 보고를 권위 위치로 검증한 뒤 호출.
    // 좌표는 원시타입으로 전달(Vector3 헤더 의존 회피). Lua: OnEnterEventArea(eventKey, objectId, x, y, z).
    void CallOnEnterEventArea(int32 eventKey, int64 objectId, float x, float y, float z);
    void CallOnExitEventArea(int32 eventKey, int64 objectId, float x, float y, float z);

    // 오브젝트(prop) 상호작용 — Stage 가 근접+키 보고를 위치 검증한 뒤 호출. Lua: OnObjectInteract(propKey, objectId).
    void CallOnObjectInteract(int32 propKey, int64 objectId);

    // 몬스터 사망 시 Stage 가 호출. (watch 등록분만 Lua 진입 — 대량몹 부하 방지.)
    //   · monsterKey 가 WatchMonsterDeath 등록 → OnMonsterDead 멀티캐스트
    //   · spawnerKey(!=0) 가 WatchSpawnerDeath 등록 → OnSpawnerMonsterDead 멀티캐스트
    //   · monsterKey 사망 대기 시퀀스(WaitForMonsterDead) 재개
    void CallOnMonsterDead(int64 objectId, int32 monsterKey, int32 spawnerKey, int64 killerObjectId);

    // 매 tick(컨텐츠 스레드) 호출. 등록된 타이머의 만기를 누적시간으로 판정해 콜백을 호출한다.
    void Update(int64 deltaMs);

private:
    // 코루틴 시퀀스를 1회 재개하고 다음 대기/종료 상태를 반영한다 (index = m_pImpl->sequences).
    void advanceSequence(size_t index);

    // 조건 대기(WaitForCount/WaitForSpawnerClear) 폴링용. m_pStage 의 몬스터 컨테이너를 센다(시체 제외).
    int aliveMonsterCount() const;
    int spawnerAliveCount(int32 spawnerKey) const;

    struct Impl;                       // .cpp 에서 정의 (sol/Lua 은닉)
    std::unique_ptr<Impl> m_pImpl;
    Stage* m_pStage = nullptr;
};
