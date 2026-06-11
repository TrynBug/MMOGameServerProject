#pragma once

#include "pch.h"

#include <vector>

// Detour 전방선언. 세부는 .cpp 에서만 필요.
class dtNavMesh;
class dtNavMeshQuery;
class dtQueryFilter;

// ─────────────────────────────────────────────────────────────
// StageNavMesh
// ─────────────────────────────────────────────────────────────
//
// 하나의 Stage 에 종속되는 NavMesh 길찾기 헬퍼.
//
// 소유 관계:
//   - dtNavMesh 자체는 NavMeshManager 가 소유 (이 클래스는 const 포인터로 참조만).
//   - dtNavMeshQuery, dtQueryFilter 는 이 클래스가 소유 (소멸자에서 해제).
//
// 스레드 안전성:
//   - Stage 당 스레드 1개 = Stage 당 StageNavMesh 1개 = Query 1개. 락 없음.
//   - 다른 Stage 에서도 동일 dtNavMesh 를 참조해도 OK (각자 자기 Query 보유).
//
// 사용 흐름:
//   1. Stage 가 NavMeshManager 로부터 const dtNavMesh* 얻음
//   2. std::make_unique<StageNavMesh>(pNavMesh) 로 생성
//   3. pNavMesh 가 nullptr 이거나 init 실패 시 IsReady()==false
//   4. FindPath / SamplePosition / (향후) ClampToNavMesh 호출
class StageNavMesh
{
public:
    // pNavMesh 가 nullptr 이면 길찾기 비활성화 상태로 생성된다 (IsReady()==false).
    explicit StageNavMesh(const dtNavMesh* pNavMesh);
    ~StageNavMesh();

    StageNavMesh(const StageNavMesh&) = delete;
    StageNavMesh& operator=(const StageNavMesh&) = delete;

    // Query 초기화 성공 여부. false 면 FindPath 등의 호출이 실패한다.
    bool IsReady() const { return m_pNavQuery != nullptr && m_pNavFilter != nullptr; }

    // 길찾기. (startX,Y,Z) -> (endX,Y,Z) waypoint 리스트 추출.
    // 좌표계: Unity 와 동일. Y 는 높이.
    // outWaypoints 는 (x, y, z) 세트 순서로 straightCount * 3 개 float 을 포함.
    //   (내부에서 clear 후 push_back.)
    // 리턴: 경로 찾으면 true, 실패시 false (outWaypoints 비어있음).
    //
    // 내부 절차 (클라 NavMeshRuntime.FindPath 와 동일):
    //   1. findNearestPoly 로 start/end 폴리곤 찾기
    //   2. findPath 로 폴리곤 시퀀스 (corridor)
    //   3. findStraightPath 로 string-pulling 된 직선 경로 (waypoint 리스트)
    bool FindPath(float startX, float startY, float startZ, float endX, float endY, float endZ, std::vector<float>& outWaypoints) const;

    // 주어진 (x,y,z) 근처의 NavMesh 표면 점을 찾는다 (Y 스냅 + walkable 검증용).
    // 검색 박스(halfExtent, 각 축 반경) 안에서 가장 가까운 NavMesh 폴리곤 위의 점을 out* 에 채운다.
    // 입력 Y 를 신뢰할 수 없는 경우(예: 스폰) halfExtentY 를 넉넉히 잡으면 바닥 높이로 스냅된다.
    // 리턴: NavMesh 준비됨 + 박스 안에 폴리곤 있으면 true(out* 채움), 아니면 false(out* 미변경).
    bool SamplePosition(float x, float y, float z, float halfExtentX, float halfExtentY, float halfExtentZ, float& outX, float& outY, float& outZ) const;

    // (cx,cy,cz) 중심 반경 radius 안의 walkable 랜덤 좌표를 찾는다 (밀도존 스폰 배치용).
    // dtNavMeshQuery::findRandomPointAroundCircle 래핑.
    // 리턴: NavMesh 준비됨 + 시작 폴리곤 + 랜덤 폴리곤 찾으면 true(out* 채움), 아니면 false(out* 미변경).
    bool SampleRandomPoint(float cx, float cy, float cz, float radius, float& outX, float& outY, float& outZ) const;

private:
    const dtNavMesh* m_pNavMesh  = nullptr;   // 참조만 (소유 안 함)
    dtNavMeshQuery*  m_pNavQuery = nullptr;   // 소유
    dtQueryFilter*   m_pNavFilter = nullptr;  // 소유
};
