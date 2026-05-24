#include "pch.h"
#include "NavMeshManager.h"

#include "nlohmann/json.hpp"  // nlohmann/json

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <vector>

// ─────────────────────────────────────────────────────────────
// 파일 포맷 정의 (Recast/Detour 공식 데모 Sample_TileMesh)
// ─────────────────────────────────────────────────────────────
//
// 파일 시작에 NavMeshSetHeader 1개, 그 뒤로 tileCount 만큼
// (NavMeshTileHeader + tileData) 가 이어진다.
// Unity 측 DotRecast 데모의 NavMesh export도 같은 포맷.
namespace
{
    constexpr int32 k_navMeshSetMagic   = 'M'<<24 | 'S'<<16 | 'E'<<8 | 'T';   // 'MSET'
    constexpr int32 k_navMeshSetVersion = 1;

    struct NavMeshSetHeader
    {
        int32       magic;
        int32       version;
        int32       numTiles;
        dtNavMeshParams params;
    };

    struct NavMeshTileHeader
    {
        dtTileRef tileRef;
        int32     dataSize;
    };

    // 메타 검증 시 부동소수 비교 허용 오차. NavMesh 단위는 m 이므로 1mm.
    constexpr double k_metaBoundsEpsilon = 1e-3;
}


NavMeshManager::~NavMeshManager()
{
    Clear();
}

bool NavMeshManager::LoadAll(const std::filesystem::path& dirPath)
{
    namespace fs = std::filesystem;

    std::error_code ec;
    if (!fs::exists(dirPath, ec) || !fs::is_directory(dirPath, ec))
    {
        LOG_WRITE(LogLevel::Error, std::format("NavMeshManager::LoadAll - directory not found. path={}",
            dirPath.string()));
        return false;
    }

    int32 successCount = 0;
    int32 failCount = 0;

    for (const auto& entry : fs::directory_iterator(dirPath, ec))
    {
        if (!entry.is_regular_file())
            continue;

        const fs::path& filePath = entry.path();
        if (filePath.extension() != ".bin")
            continue;

        const std::string name = filePath.stem().string();

        // 중복 키 검사.
        if (m_navMeshes.find(name) != m_navMeshes.end())
        {
            LOG_WRITE(LogLevel::Error, std::format("NavMeshManager::LoadAll - duplicate navmesh name. name={} path={}",
                name, filePath.string()));
            ++failCount;
            continue;
        }

        dtNavMesh* pNavMesh = loadNavMeshFromFile(filePath);
        if (!pNavMesh)
        {
            // 상세 사유는 loadNavMeshFromFile 내부에서 로그됨.
            ++failCount;
            continue;
        }

        m_navMeshes[name] = pNavMesh;
        ++successCount;

        LOG_WRITE(LogLevel::Info, std::format("NavMeshManager::LoadAll - loaded. name={} path={}",
            name, filePath.string()));

        // ── 메타 로드 + 검증 ─────────────────────────────────────
        // 같은 디렉토리의 "<binFileName>.navmeta.json" 을 시도.
        // (예: NetworkTestScene.bin → NetworkTestScene.bin.navmeta.json)
        // 메타가 없으면 그냥 패스. 있으면 .bin 실제 데이터와 대조 검증.
        const fs::path metaPath = filePath.string() + ".navmeta.json";
        if (!fs::exists(metaPath, ec))
        {
            LOG_WRITE(LogLevel::Warn, std::format("NavMeshManager::LoadAll - meta file not found. name={} metaPath={}",
                name, metaPath.string()));
            continue;
        }

        NavMeshMeta meta;
        if (!loadMetaFromFile(metaPath, meta))
        {
            // 파싱 실패. 메타 등록 안 함. 로그는 loadMetaFromFile 안에서.
            continue;
        }

        const NavMeshMeta actual = computeMetaFromNavMesh(pNavMesh);
        validateMeta(name, meta, actual);

        // 검증 결과와 무관하게 메타 등록 (서버는 메타값을 사용).
        m_navMeshMetas[name] = meta;

        LOG_WRITE(LogLevel::Info, std::format(
            "NavMeshManager::LoadAll - meta loaded. name={} bounds=({:.3f},{:.3f})~({:.3f},{:.3f}) tilesCount={} walkablePolygonsCount={}",
            name, meta.minX, meta.minZ, meta.maxX, meta.maxZ, meta.tilesCount, meta.walkablePolygonsCount));
    }

    LOG_WRITE(LogLevel::Info, std::format("NavMeshManager::LoadAll - done. dir={} success={} fail={} metaCount={}",
        dirPath.string(), successCount, failCount, static_cast<int32>(m_navMeshMetas.size())));

    return successCount > 0;
}

const dtNavMesh* NavMeshManager::Find(const std::string& name) const
{
    auto iter = m_navMeshes.find(name);
    if (iter == m_navMeshes.end())
        return nullptr;
    return iter->second;
}

const NavMeshMeta* NavMeshManager::FindMeta(const std::string& name) const
{
    auto iter = m_navMeshMetas.find(name);
    if (iter == m_navMeshMetas.end())
        return nullptr;
    return &iter->second;
}

void NavMeshManager::Clear()
{
    for (auto& [name, pNavMesh] : m_navMeshes)
    {
        if (pNavMesh)
            dtFreeNavMesh(pNavMesh);
    }
    m_navMeshes.clear();
    m_navMeshMetas.clear();
}

dtNavMesh* NavMeshManager::loadNavMeshFromFile(const std::filesystem::path& filePath)
{
    // RAII 없이 C FILE* 사용: 중간 return 마다 fclose 호출 필요. 함수 짧으니 명시적으로 처리.
    FILE* pFile = nullptr;
    if (_wfopen_s(&pFile, filePath.wstring().c_str(), L"rb") != 0 || !pFile)
    {
        LOG_WRITE(LogLevel::Error, std::format("NavMeshManager::loadNavMeshFromFile - failed to open. path={}",
            filePath.string()));
        return nullptr;
    }

    // 1. Set 헤더 읽기.
    NavMeshSetHeader header;
    if (std::fread(&header, sizeof(NavMeshSetHeader), 1, pFile) != 1)
    {
        LOG_WRITE(LogLevel::Error, std::format("NavMeshManager::loadNavMeshFromFile - failed to read header. path={}",
            filePath.string()));
        std::fclose(pFile);
        return nullptr;
    }

    if (header.magic != k_navMeshSetMagic)
    {
        LOG_WRITE(LogLevel::Error, std::format("NavMeshManager::loadNavMeshFromFile - invalid magic. path={} magic=0x{:x}",
            filePath.string(), header.magic));
        std::fclose(pFile);
        return nullptr;
    }
    if (header.version != k_navMeshSetVersion)
    {
        LOG_WRITE(LogLevel::Error, std::format("NavMeshManager::loadNavMeshFromFile - unsupported version. path={} version={}",
            filePath.string(), header.version));
        std::fclose(pFile);
        return nullptr;
    }
    if (header.numTiles <= 0)
    {
        LOG_WRITE(LogLevel::Error, std::format("NavMeshManager::loadNavMeshFromFile - numTiles <= 0. path={} numTiles={}",
            filePath.string(), header.numTiles));
        std::fclose(pFile);
        return nullptr;
    }

    LOG_WRITE(LogLevel::Info, std::format(
        "NavMeshManager::loadNavMeshFromFile - header ok. path={} magic=0x{:x} version={} numTiles={} "
        "orig=({:.3f},{:.3f},{:.3f}) tileW={:.3f} tileH={:.3f} maxTiles={} maxPolys={}",
        filePath.string(), static_cast<uint32>(header.magic), header.version, header.numTiles,
        header.params.orig[0], header.params.orig[1], header.params.orig[2],
        header.params.tileWidth, header.params.tileHeight,
        header.params.maxTiles, header.params.maxPolys));

    // 2. dtNavMesh 생성 및 init.
    dtNavMesh* pNavMesh = dtAllocNavMesh();
    if (!pNavMesh)
    {
        LOG_WRITE(LogLevel::Error, std::format("NavMeshManager::loadNavMeshFromFile - dtAllocNavMesh failed. path={}",
            filePath.string()));
        std::fclose(pFile);
        return nullptr;
    }

    dtStatus status = pNavMesh->init(&header.params);
    if (dtStatusFailed(status))
    {
        LOG_WRITE(LogLevel::Error, std::format("NavMeshManager::loadNavMeshFromFile - dtNavMesh::init failed. path={} status=0x{:x}",
            filePath.string(), static_cast<uint32>(status)));
        dtFreeNavMesh(pNavMesh);
        std::fclose(pFile);
        return nullptr;
    }

    // 3. tile 데이터 읽기.
    int32 addedCount = 0;
    int32 skippedCount = 0;
    for (int32 i = 0; i < header.numTiles; ++i)
    {
        NavMeshTileHeader tileHeader;
        if (std::fread(&tileHeader, sizeof(NavMeshTileHeader), 1, pFile) != 1)
        {
            LOG_WRITE(LogLevel::Error, std::format("NavMeshManager::loadNavMeshFromFile - failed to read tile header. path={} tileIndex={}",
                filePath.string(), i));
            dtFreeNavMesh(pNavMesh);
            std::fclose(pFile);
            return nullptr;
        }

        if (tileHeader.dataSize <= 0 || !tileHeader.tileRef)
        {
            // 빈 tile은 skip (Sample_TileMesh의 표준 동작).
            ++skippedCount;
            continue;
        }

        // tile 데이터 버퍼는 dtNavMesh가 소유하기 위해 dtAlloc로 할당해야 한다.
        // (dtNavMesh가 addTile 시 DT_TILE_FREE_DATA 플래그를 받으면 자기가 dtFree로 해제함.)
        unsigned char* pTileData = static_cast<unsigned char*>(dtAlloc(tileHeader.dataSize, DT_ALLOC_PERM));
        if (!pTileData)
        {
            LOG_WRITE(LogLevel::Error, std::format("NavMeshManager::loadNavMeshFromFile - dtAlloc failed. path={} tileIndex={} dataSize={}",
                filePath.string(), i, tileHeader.dataSize));
            dtFreeNavMesh(pNavMesh);
            std::fclose(pFile);
            return nullptr;
        }

        if (std::fread(pTileData, tileHeader.dataSize, 1, pFile) != 1)
        {
            LOG_WRITE(LogLevel::Error, std::format("NavMeshManager::loadNavMeshFromFile - failed to read tile data. path={} tileIndex={} dataSize={}",
                filePath.string(), i, tileHeader.dataSize));
            dtFree(pTileData);
            dtFreeNavMesh(pNavMesh);
            std::fclose(pFile);
            return nullptr;
        }

        status = pNavMesh->addTile(pTileData, tileHeader.dataSize, DT_TILE_FREE_DATA, tileHeader.tileRef, nullptr);
        if (dtStatusFailed(status))
        {
            // tileData 의 첫 4바이트 조사 (Detour tile magic 은 'DNAV' = 0x56414e44 이어야 함)
            uint32 tileDataMagic = 0;
            if (tileHeader.dataSize >= 4)
                std::memcpy(&tileDataMagic, pTileData, 4);

            LOG_WRITE(LogLevel::Error, std::format(
                "NavMeshManager::loadNavMeshFromFile - addTile failed. path={} tileIndex={} status=0x{:x} "
                "tileDataMagic=0x{:x} (expect 0x56414e44 'DNAV')",
                filePath.string(), i, static_cast<uint32>(status), tileDataMagic));
            // addTile 실패 시 데이터는 우리가 free 해야 함 (NavMesh가 안 받음).
            dtFree(pTileData);
            dtFreeNavMesh(pNavMesh);
            std::fclose(pFile);
            return nullptr;
        }

        ++addedCount;
    }

    LOG_WRITE(LogLevel::Info, std::format(
        "NavMeshManager::loadNavMeshFromFile - tile loop done. path={} numTiles={} added={} skipped={}",
        filePath.string(), header.numTiles, addedCount, skippedCount));

    std::fclose(pFile);
    return pNavMesh;
}

NavMeshMeta NavMeshManager::computeMetaFromNavMesh(const dtNavMesh* pNavMesh)
{
    // 클라이언트 NavMeshBuilder 와 동일한 방식: 모든 유효 타일의 bmin/bmax X-Z 합으로 AABB,
    // walkable polygons 는 각 타일 header 의 polyCount 합.
    NavMeshMeta meta;
    if (!pNavMesh)
        return meta;

    double minX =  (std::numeric_limits<double>::max)();
    double minZ =  (std::numeric_limits<double>::max)();
    double maxX = -(std::numeric_limits<double>::max)();
    double maxZ = -(std::numeric_limits<double>::max)();
    int32  validTileCount = 0;
    int32  totalPolys = 0;

    const int32 maxTiles = pNavMesh->getMaxTiles();
    for (int32 i = 0; i < maxTiles; ++i)
    {
        const dtMeshTile* pTile = pNavMesh->getTile(i);
        if (!pTile || !pTile->header)
            continue;

        const dtMeshHeader* h = pTile->header;
        if (static_cast<double>(h->bmin[0]) < minX) minX = h->bmin[0];
        if (static_cast<double>(h->bmin[2]) < minZ) minZ = h->bmin[2];
        if (static_cast<double>(h->bmax[0]) > maxX) maxX = h->bmax[0];
        if (static_cast<double>(h->bmax[2]) > maxZ) maxZ = h->bmax[2];
        totalPolys += h->polyCount;
        ++validTileCount;
    }

    if (validTileCount == 0)
    {
        // 유효한 tile 이 0 개면 bounds 0으로.
        meta.minX = 0.0; meta.minZ = 0.0; meta.maxX = 0.0; meta.maxZ = 0.0;
    }
    else
    {
        meta.minX = minX;
        meta.minZ = minZ;
        meta.maxX = maxX;
        meta.maxZ = maxZ;
    }
    meta.tilesCount = validTileCount;
    meta.walkablePolygonsCount = totalPolys;
    return meta;
}

bool NavMeshManager::loadMetaFromFile(const std::filesystem::path& metaPath, NavMeshMeta& outMeta)
{
    std::ifstream ifs(metaPath, std::ios::in | std::ios::binary);
    if (!ifs.is_open())
    {
        LOG_WRITE(LogLevel::Error, std::format("NavMeshManager::loadMetaFromFile - failed to open. path={}",
            metaPath.string()));
        return false;
    }

    nlohmann::json j;
    try
    {
        ifs >> j;
    }
    catch (const std::exception& ex)
    {
        LOG_WRITE(LogLevel::Error, std::format("NavMeshManager::loadMetaFromFile - json parse failed. path={} reason={}",
            metaPath.string(), ex.what()));
        return false;
    }

    try
    {
        const auto& bounds = j.at("bounds");
        outMeta.minX = bounds.at("min_x").get<double>();
        outMeta.minZ = bounds.at("min_z").get<double>();
        outMeta.maxX = bounds.at("max_x").get<double>();
        outMeta.maxZ = bounds.at("max_z").get<double>();
        outMeta.tilesCount = j.at("tiles_count").get<int32>();
        outMeta.walkablePolygonsCount = j.at("walkable_polygons_count").get<int32>();
    }
    catch (const std::exception& ex)
    {
        LOG_WRITE(LogLevel::Error, std::format("NavMeshManager::loadMetaFromFile - missing/invalid field. path={} reason={}",
            metaPath.string(), ex.what()));
        return false;
    }

    return true;
}

void NavMeshManager::validateMeta(const std::string& name, const NavMeshMeta& meta, const NavMeshMeta& actual)
{
    auto closeEnough = [](double a, double b)
    {
        return std::abs(a - b) <= k_metaBoundsEpsilon;
    };

    const bool boundsOk = closeEnough(meta.minX, actual.minX)
                       && closeEnough(meta.minZ, actual.minZ)
                       && closeEnough(meta.maxX, actual.maxX)
                       && closeEnough(meta.maxZ, actual.maxZ);
    const bool tilesOk = meta.tilesCount == actual.tilesCount;
    const bool polysOk = meta.walkablePolygonsCount == actual.walkablePolygonsCount;

    if (boundsOk && tilesOk && polysOk)
        return;

    LOG_WRITE(LogLevel::Error, std::format(
        "NavMeshManager::validateMeta - mismatch. name={} "
        "metaBounds=({:.3f},{:.3f})~({:.3f},{:.3f}) actualBounds=({:.3f},{:.3f})~({:.3f},{:.3f}) "
        "metaTiles={} actualTiles={} metaPolys={} actualPolys={} "
        "(NavMesh 를 다시 빌드해 주세요. 서버는 메타 bounds 를 사용해 계속 진행합니다.)",
        name,
        meta.minX, meta.minZ, meta.maxX, meta.maxZ,
        actual.minX, actual.minZ, actual.maxX, actual.maxZ,
        meta.tilesCount, actual.tilesCount,
        meta.walkablePolygonsCount, actual.walkablePolygonsCount));
}
