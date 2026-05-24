#pragma once

#include "pch.h"

#include <filesystem>
#include <string>
#include <unordered_map>

// Detour
#include "DetourNavMesh.h"


// ─────────────────────────────────────────────────────────────
// NavMeshMeta
// ─────────────────────────────────────────────────────────────
//
// .bin 옆에 나란히 있는 .navmeta.json 에서 읽은 메타정보.
// Stage 초기화 시 sector grid 의 world bounds 로 사용된다.
//
// bounds 는 X-Z 평면만 보유한다 (서버 sector grid 는 2D). Y 는 메타에 아예 들어있지 않다.
struct NavMeshMeta
{
    double minX = 0.0;
    double minZ = 0.0;
    double maxX = 0.0;
    double maxZ = 0.0;
    int32  tilesCount = 0;
    int32  walkablePolygonsCount = 0;
};


// ─────────────────────────────────────────────────────────────
// NavMeshManager
// ─────────────────────────────────────────────────────────────
//
// 게임서버 시작 시 모든 NavMesh .bin 파일을 로드하여 dtNavMesh 객체로 관리한다.
// - GameServer가 소유 (싱글톤 아님).
// - LoadAll(dirPath)로 디렉토리 내의 모든 .bin을 1회 로드한다.
//   각 .bin 옆의 .navmeta.json 이 있으면 메타도 함께 로드하고,
//   .bin 의 실제 데이터와 대조 검증한다. 불일치는 ERROR 로그만 남긴다 (서버는 계속 진행).
// - Find(name)로 파일명(확장자 제외) 기준 dtNavMesh 포인터를 조회한다.
// - FindMeta(name)로 같은 키로 메타를 조회한다. 해당 .navmeta.json 이 없었으면 nullptr.
//
// 라이프타임 / 소유권:
// - dtNavMesh의 소유자는 NavMeshManager 단 하나.
// - Stage 등 사용자는 const dtNavMesh* 참조만 보관한다. 절대 dtFreeNavMesh 하지 말 것.
// - NavMeshManager가 살아있는 동안 dtNavMesh 포인터는 유효하다.
// - Clear() 또는 소멸자에서 모든 dtNavMesh가 dtFreeNavMesh로 해제된다.
//
// 스레드 안전성:
// - LoadAll/Clear는 게임서버 시작/종료 시점에만 호출되는 단일 스레드 동작 가정.
// - Find/FindMeta는 LoadAll 이후 readonly 접근만 발생하므로 mutex 없이 안전.
// - 사용자가 받아간 dtNavMesh는 변경 불가 (수정 금지).
//   같은 dtNavMesh에 대해 여러 스레드가 각자의 dtNavMeshQuery로 path query만 수행하는 것은 안전.
//
// 파일 포맷:
// - Recast/Detour 공식 데모(Sample_TileMesh)의 멀티타일 .bin 포맷을 따른다.
//   [NavMeshSetHeader][NavMeshTileHeader + tileData] * tileCount
// - Unity 측 DotRecast 데모 export와 호환된다.
// - 메타는 "<binFileName>.navmeta.json" 으로 같은 디렉토리에 있는 것으로 기대한다.
class NavMeshManager
{
public:
    NavMeshManager() = default;
    ~NavMeshManager();

    NavMeshManager(const NavMeshManager&) = delete;
    NavMeshManager& operator=(const NavMeshManager&) = delete;

    // 디렉토리 내 모든 .bin 파일을 로드한다 (하위 폴더는 탐색하지 않음).
    // 각 파일은 파일명(확장자 제외)을 키로 등록된다.
    // - 같은 이름의 .navmeta.json 이 있으면 같이 로드한다. (없으면 메타는 등록 안함)
    // - 메타가 있으면 .bin 실제 데이터와 대조 검증. 불일치 시 ERROR 로그만 남김 (서버 계속 진행).
    // - 중복 키가 있으면 나중에 로드된 것은 거부하고 에러 로그.
    // - 개별 파일 로드 실패는 에러 로그를 남기고 다음 파일로 진행 (전체 실패는 아님).
    // 반환: 1개라도 성공적으로 로드되었으면 true, 모두 실패했거나 디렉토리 자체가 없으면 false.
    bool LoadAll(const std::filesystem::path& dirPath);

    // 이름(파일명, 확장자 제외)으로 NavMesh 참조 조회. 없으면 nullptr.
    // 사용자는 이 포인터를 dtFreeNavMesh 하면 안 된다.
    const dtNavMesh* Find(const std::string& name) const;

    // 이름(파일명, 확장자 제외)으로 NavMesh 메타 조회. 메타 파일이 없었으면 nullptr.
    const NavMeshMeta* FindMeta(const std::string& name) const;

    // 현재 로드된 NavMesh 개수.
    size_t GetCount() const { return m_navMeshes.size(); }

    // 모든 NavMesh를 해제하고 맵을 비운다. GameServer Shutdown 흐름에서 호출.
    void Clear();

private:
    // 단일 .bin 파일을 로드하여 dtNavMesh를 생성한다. 실패 시 nullptr.
    // 호출자가 반환된 dtNavMesh의 소유권을 가짐.
    static dtNavMesh* loadNavMeshFromFile(const std::filesystem::path& filePath);

    // .bin 의 모든 타일을 순회해 X-Z bounds / 타일수 / walkable polygon 수를 계산한다.
    // 메타 검증용.
    static NavMeshMeta computeMetaFromNavMesh(const dtNavMesh* pNavMesh);

    // .navmeta.json 파일을 파싱한다. 실패 시 false 리턴 및 로그.
    static bool loadMetaFromFile(const std::filesystem::path& metaPath, NavMeshMeta& outMeta);

    // 메타(메타파일에서 읽은 것) vs actual(.bin 순회해 계산한 것) 검증. 불일치 시 ERROR 로그.
    // 검증 실패해도 서버는 계속 진행하므로 리턴값은 없다.
    static void validateMeta(const std::string& name, const NavMeshMeta& meta, const NavMeshMeta& actual);

private:
    // 파일명(확장자 제외) → dtNavMesh* 매핑. NavMeshManager가 소유.
    std::unordered_map<std::string, dtNavMesh*> m_navMeshes;

    // 파일명(확장자 제외) → NavMeshMeta 매핑. .navmeta.json 이 있었던 항목만 등록.
    std::unordered_map<std::string, NavMeshMeta> m_navMeshMetas;
};
