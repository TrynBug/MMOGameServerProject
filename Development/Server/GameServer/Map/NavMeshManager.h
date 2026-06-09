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
// NavMesh .bin 파일과 같이 있는 .navmeta.json 에서 읽은 메타정보
// Stage 초기화 시 sector grid 의 world bounds(최대 최소 X, Z 값)로 사용된다. sector grid는 평면이라서 X-Z 좌표만 가진다. Y는 높이라서 필요없음.
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
// - GameServer가 소유 (싱글톤 아님)
// - LoadAll(dirPath) 함수로 디렉토리 내의 모든 .bin을 로드한다. .navmeta.json 메타도 같이 로드한다.
// - Find(name) 함수로 파일명(확장자 제외) 기준 dtNavMesh 포인터를 조회한다.
//
// 라이프타임 / 소유권:
// - dtNavMesh는 NavMeshManager 만 소유한다.
// - Stage 등에서는 const dtNavMesh* 참조만 보관한다. (절대 dtFreeNavMesh 하지 말 것)
// - NavMeshManager가 살아있는 동안 dtNavMesh 포인터는 유효하다.
//
// 스레드 안전성:
// - LoadAll, Clear는 thread-safe 하지 않다. 게임서버 시작, 종료 시점에 1회만 호출해야 한다.
// - Find, FindMeta 함수는 read만 하기 때문에 안전함.
// - 사용자가 획득한 dtNavMesh 참조는 절대 수정해서는 안된다.
// - 같은 dtNavMesh에 대해 여러 스레드가 각자의 dtNavMeshQuery로 path query만 수행하는 것은 안전하다.
class NavMeshManager
{
public:
    NavMeshManager() = default;
    ~NavMeshManager();

    NavMeshManager(const NavMeshManager&) = delete;
    NavMeshManager& operator=(const NavMeshManager&) = delete;

public:
    // 디렉토리 내 모든 .bin 파일, .navmeta.json 파일을 로드한다 (하위 폴더는 탐색하지 않음).
    // 반환값: 1개라도 실패했거나 디렉토리 자체가 없으면 false
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
