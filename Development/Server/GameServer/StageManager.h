#pragma once

#include "pch.h"
#include "SystemStage.h"
#include "Town.h"
#include "Field.h"
#include "ThreadSafeUnorderedMap.h"

class GameServer;

// ─────────────────────────────────────────────────────────────
// StageManager
// ─────────────────────────────────────────────────────────────
//
// 게임서버 내 모든 Stage(SystemStage, Town, Field, Dungeon 등)를 관리한다.
// - GameServer가 소유 (m_stageManager 멤버).
// - Stage 생성 + 등록 + 컨텐츠 스레드 배정 + GameServer 주입까지 한 번에 처리한다.
// - 다른 코드는 StageManager를 통해 Stage를 조회/순회한다.
//
// 라이프타임:
// - 모든 Stage의 강한 소유자.
// - Clear() 호출 시 AssignContents 등록도 풀고, 내부 map을 비운다.
//
// 스레드:
// - m_safeStages는 SharedThreadSafeUnorderedMap이므로 멀티 스레드 접근 안전.
// - 고정 Stage 캐시(m_spSystemStage, m_spTown)는 Initialize 후 readonly이므로 mutex 없이 접근.
class StageManager
{
public:
    StageManager() = default;
    ~StageManager() = default;

    StageManager(const StageManager&) = delete;
    StageManager& operator=(const StageManager&) = delete;

    // GameServer 포인터와 컨텐츠 스레드 개수를 받아 초기화.
    // - pGameServer: Stage가 GameServer 서비스(패킷 전송 등)를 호출하기 위해 필요.
    // - contentsThreadCount: Stage를 컨텐츠 스레드에 배정할 때 mod 연산에 사용.
    void Initialize(GameServer* pGameServer, int32 contentsThreadCount);

    // 등록된 모든 Stage를 컨텐츠 스레드에서 제거하고 내부 map을 비운다.
    // GameServer Shutdown 흐름에서 호출.
    void Clear();

    // ── Stage 생성 + 등록 + 컨텐츠 스레드 배정 + GameServer 주입 ──
    // 실패 시 nullptr 리턴 (중복 stageId 등).
    SystemStagePtr CreateSystemStage(int64 stageId, int32 stageDataKey);
    TownPtr        CreateTown(int64 stageId, int32 stageDataKey);
    FieldPtr       CreateField(int64 stageId, int32 stageDataKey);
    // 향후: CreateUserDungeon, CreatePublicDungeon 등

    // ── 조회 ──
    // Find: 모든 종류의 Stage에 대해 동일하게 lookup. 못 찾으면 nullptr.
    StagePtr Find(int64 stageId) const;

    // dataKey로 Stage 인스턴스 목록 조회.
    // 동일 dataKey의 Stage가 여러 개 존재할 수 있다는 전제 (채널, 인스턴스 던전).
    // 호출부가 복수 전제를 잊지 않도록 단수 반환 API는 의도적으로 두지 않는다.
    std::vector<StagePtr> FindStagesByDataKey(int32 stageDataKey) const;

    // 자주 쓰는 고정 Stage의 빠른 접근. Initialize 후 readonly.
    SystemStagePtr GetSystemStage() const { return m_spSystemStage; }
    TownPtr        GetTown() const        { return m_spTown; }

private:
    // stageId를 컨텐츠 스레드 인덱스로 매핑.
    int32 computeStageThreadIndex(int64 stageId) const;

    // 공용 등록 경로: 컨텐츠 스레드 배정 + m_safeStages + dataKey 인덱스.
    // 모든 Create* 가 이 함수를 거친다 (인덱스 누락 방지).
    void registerStage(int64 stageId, int32 stageDataKey, const StagePtr& spStage);

    // NavMesh 기반 Stage(Town/Field)의 공통 준비:
    // GameData 조회 → 중복 stageId 검사 → params 구성 + NavMesh 메타로 bounds 덮어쓰기.
    // 실패 시 false (에러 로그는 내부에서 남김). outNavMesh는 nullptr일 수 있음 (길찾기 비활성화).
    bool prepareNavStage(int64 stageId, int32 stageDataKey, const char* logTag,
                         StageGridParams& outParams, const dtNavMesh*& outNavMesh);

    GameServer* m_pGameServer = nullptr;
    int32 m_contentsThreadCount = 0;

    // 모든 Stage의 강한 소유자. key=stageId, value=StagePtr.
    SharedThreadSafeUnorderedMap<int64, StagePtr> m_safeStages;

    // dataKey → stageId 목록 보조 인덱스 (소유 없음).
    // 등록은 registerStage, 제거는 Clear(향후 DestroyStage)에서만 갱신한다.
    // m_safeStages와 별도 락이므로 "인덱스에는 있는데 m_safeStages에 없는" 순간이 없도록
    // 등록 시 m_safeStages 먼저, 제거 시 인덱스 먼저 갱신한다 (조회 측은 miss만 발생).
    mutable std::mutex m_dataKeyIndexMutex;
    std::unordered_map<int32, std::vector<int64>> m_stageIdsByDataKey;

    // 자주 쓰는 고정 Stage의 캐시. m_safeStages에도 들어가 있음.
    // Initialize 시점에 set되고 그 후엔 변경되지 않으므로 mutex 없이 접근 가능.
    SystemStagePtr m_spSystemStage;
    TownPtr        m_spTown;
};
