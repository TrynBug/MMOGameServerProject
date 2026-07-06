using System;
using System.IO;
using UnityEngine;

namespace Client.Game
{
    // 디버그용 sector 격자를 게임 화면에 그린다.
    //
    // 목적:
    //   몬스터 스폰/디스폰이 sector 경계에서 일어나는 것을 눈으로 확인하기 위함.
    //   따라서 격자는 서버 Stage 의 sector 격자와 "정확히" 정렬되어야 한다.
    //
    // 정렬 방법 (서버 Stage::initializeSectorGrid 와 동일하게 맞춤):
    //   - 원점/범위(worldMin/Max): NavMesh 메타(.navmeta.json)의 bounds. (서버도 같은 메타를 씀)
    //   - 간격(sectorSize): GameData_Stage.sectorSize. (호출자가 전달)
    //   - sector 개수: ceil((max - min) / sectorSize). 격자선은 min + k*sectorSize.
    //
    // 사용:
    //   SectorGridDebug.ShowForStage(navMeshFileName, sectorSize, groundY);  // StageEnterNtf 처리 시(파라미터 캐시)
    //   SectorGridDebug.SetEnabled(true) / Toggle();                          // 치트 토글로 표시 on/off
    //
    // 표시 여부는 정적 토글 s_enabled 로 제어한다. 기본값은 꺼짐(off) — StageEnterNtf 는 파라미터만
    // 캐시하고, 토글이 켜져 있을 때만 실제로 격자를 그린다. 치트 콘솔의 "sectorgrid" 명령이 토글한다.
    //
    // 한 시점에 하나만 존재(싱글톤). 라인 메시 1개를 MeshRenderer 로 그린다(게임 뷰에서 보임).
    public class SectorGridDebug : MonoBehaviour
    {
        // 바닥과 z-fighting 안 나도록 살짝 띄우는 높이.
        private const float k_yLift = 0.1f;

        private static SectorGridDebug sm_instance;

        // 표시 토글. 기본 꺼짐. 치트 콘솔의 "sectorgrid" 로 켜고 끈다.
        private static bool s_enabled;

        // 마지막 스테이지 파라미터 캐시. 토글을 켤 때 스테이지 재진입 없이 다시 그리기 위함.
        private static string s_navMeshFileName;
        private static double s_sectorSize;
        private static float s_groundY;
        private static bool s_hasParams;

        private MeshFilter m_meshFilter;
        private Material m_material;
        private Mesh m_mesh;

        // 메타 JSON 파싱용. JsonUtility 는 필드명이 JSON 키와 정확히 일치해야 채워준다.
        [Serializable] private class MetaBounds { public float min_x; public float min_z; public float max_x; public float max_z; }
        [Serializable] private class NavMeta { public string navmesh_file; public MetaBounds bounds; }

        public static bool IsEnabled => s_enabled;

        // StageEnterNtf 처리 시 호출. 스테이지 파라미터를 캐시하고, 토글이 켜져 있을 때만 실제로 그린다.
        // (기본 토글 off 이므로 평소엔 캐시만 하고 아무것도 그리지 않는다.)
        public static void ShowForStage(string navMeshFileName, double sectorSize, float groundY)
        {
            s_navMeshFileName = navMeshFileName;
            s_sectorSize = sectorSize;
            s_groundY = groundY;
            s_hasParams = true;
            applyState();
        }

        // 치트 토글: on/off 명시.
        public static void SetEnabled(bool enabled)
        {
            s_enabled = enabled;
            applyState();
        }

        // 치트 토글: 현재 상태를 뒤집고 결과(켜짐 여부)를 반환.
        public static bool Toggle()
        {
            s_enabled = !s_enabled;
            applyState();
            return s_enabled;
        }

        public static void Hide()
        {
            if (sm_instance != null)
                sm_instance.clear();
        }

        // 토글/캐시 상태에 따라 격자를 그리거나 지운다.
        // 켜짐 + 캐시된 스테이지 파라미터가 있으면 그리고, 아니면 지운다.
        private static void applyState()
        {
            if (s_enabled && s_hasParams)
            {
                ensureInstance();
                sm_instance.show(s_navMeshFileName, s_sectorSize, s_groundY);
            }
            else
            {
                Hide();
            }
        }

        private static void ensureInstance()
        {
            if (sm_instance != null)
                return;
            var go = new GameObject("[SectorGridDebug]");
            go.AddComponent<SectorGridDebug>();   // Awake 에서 sm_instance 세팅
        }

        private void Awake()
        {
            if (sm_instance != null && sm_instance != this)
            {
                Destroy(gameObject);
                return;
            }
            sm_instance = this;

            m_meshFilter = gameObject.AddComponent<MeshFilter>();
            var meshRenderer = gameObject.AddComponent<MeshRenderer>();
            meshRenderer.shadowCastingMode = UnityEngine.Rendering.ShadowCastingMode.Off;
            meshRenderer.receiveShadows = false;

            // URP 환경: Universal Render Pipeline/Unlit 단색. 못 찾으면 Sprites/Default 폴백.
            Shader shader = Shader.Find("Universal Render Pipeline/Unlit");
            if (shader == null)
                shader = Shader.Find("Sprites/Default");
            m_material = new Material(shader);
            var color = new Color(0f, 1f, 0f, 1f);   // 초록
            m_material.color = color;                          // 빌트인/Sprites 용 _Color
            if (m_material.HasProperty("_BaseColor"))
                m_material.SetColor("_BaseColor", color);      // URP/Unlit 용
            meshRenderer.material = m_material;
        }

        private void OnDestroy()
        {
            if (sm_instance == this)
                sm_instance = null;
        }

        private void clear()
        {
            if (m_mesh != null)
                m_mesh.Clear();
        }

        private void show(string navMeshFileName, double sectorSize, float groundY)
        {
            if (sectorSize <= 0.0)
            {
                Debug.LogWarning($"[SectorGridDebug] sectorSize 가 유효하지 않습니다: {sectorSize}");
                return;
            }

            if (!tryReadBounds(navMeshFileName, out MetaBounds bounds))
            {
                Debug.LogWarning($"[SectorGridDebug] NavMesh 메타 bounds 를 읽지 못했습니다: navMeshFileName={navMeshFileName}");
                return;
            }

            buildGridMesh(bounds, (float)sectorSize, groundY + k_yLift);
        }

        // 메타 파일 경로: StreamingAssets/NavMesh/<navMeshFileName>.bin.navmeta.json
        // (NavMeshService.GetBinPath 규칙 + ".navmeta.json")
        private static bool tryReadBounds(string navMeshFileName, out MetaBounds bounds)
        {
            bounds = null;
            if (string.IsNullOrEmpty(navMeshFileName))
                return false;

            string path = Path.Combine(Application.streamingAssetsPath, "NavMesh", navMeshFileName + ".bin.navmeta.json");
            if (!File.Exists(path))
            {
                Debug.LogWarning($"[SectorGridDebug] 메타 파일이 없습니다: {path}");
                return false;
            }

            try
            {
                string json = File.ReadAllText(path);
                NavMeta meta = JsonUtility.FromJson<NavMeta>(json);
                if (meta == null || meta.bounds == null)
                    return false;
                bounds = meta.bounds;
                return true;
            }
            catch (Exception ex)
            {
                Debug.LogError($"[SectorGridDebug] 메타 파싱 실패 ({path}): {ex.Message}");
                return false;
            }
        }

        // 서버와 동일: countX = ceil((maxX-minX)/sectorSize). 격자선은 minX + k*sectorSize.
        // 격자 커버 범위도 서버 sector 커버 범위([min, min + count*sectorSize])와 동일하게 그린다.
        private void buildGridMesh(MetaBounds b, float sectorSize, float y)
        {
            int countX = Mathf.CeilToInt((b.max_x - b.min_x) / sectorSize);
            int countZ = Mathf.CeilToInt((b.max_z - b.min_z) / sectorSize);
            if (countX <= 0 || countZ <= 0)
            {
                Debug.LogWarning($"[SectorGridDebug] 격자 수가 0 이하입니다. countX={countX} countZ={countZ}");
                return;
            }

            float minX = b.min_x;
            float minZ = b.min_z;
            float gridMaxX = minX + countX * sectorSize;
            float gridMaxZ = minZ + countZ * sectorSize;

            // 세로선(countX+1개) + 가로선(countZ+1개), 각 선당 2 정점.
            int vertCount = (countX + 1) * 2 + (countZ + 1) * 2;
            var verts = new Vector3[vertCount];
            var indices = new int[vertCount];

            int vi = 0;
            for (int i = 0; i <= countX; ++i)   // 세로선: x 고정, z 진행
            {
                float x = minX + i * sectorSize;
                verts[vi]     = new Vector3(x, y, minZ);
                verts[vi + 1] = new Vector3(x, y, gridMaxZ);
                vi += 2;
            }
            for (int j = 0; j <= countZ; ++j)   // 가로선: z 고정, x 진행
            {
                float z = minZ + j * sectorSize;
                verts[vi]     = new Vector3(minX, y, z);
                verts[vi + 1] = new Vector3(gridMaxX, y, z);
                vi += 2;
            }
            for (int k = 0; k < vertCount; ++k)
                indices[k] = k;

            if (m_mesh == null)
            {
                m_mesh = new Mesh { name = "SectorGrid" };
                m_meshFilter.mesh = m_mesh;
            }
            m_mesh.Clear();
            m_mesh.indexFormat = (vertCount > 65000)
                ? UnityEngine.Rendering.IndexFormat.UInt32
                : UnityEngine.Rendering.IndexFormat.UInt16;
            m_mesh.vertices = verts;
            m_mesh.SetIndices(indices, MeshTopology.Lines, 0);
            m_mesh.RecalculateBounds();

            Debug.Log($"[SectorGridDebug] grid: min=({minX:F2},{minZ:F2}) sectorSize={sectorSize} count={countX}x{countZ} y={y:F2}");
        }
    }
}
