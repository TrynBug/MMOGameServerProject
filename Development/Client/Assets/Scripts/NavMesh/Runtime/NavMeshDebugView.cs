using System.Collections.Generic;
using DotRecast.Detour;
using UnityEngine;
using UnityEngine.Rendering;

namespace MMO.Client.Navigation
{
    // 런타임에 NavMesh 폴리곤 가장자리를 게임 화면에 라인 메시로 그리는 디버그 뷰.
    //
    // NavMeshRuntimeDebug(Debug.DrawLine 기반)와 달리, 실제 Mesh 를 MeshRenderer 로 그리므로
    // GameView 의 Gizmos 토글이나 에디터 없이도 보인다(개발 빌드에서도 동일하게 보임).
    // SectorGridDebug 와 같은 패턴이다: 정적 토글(기본 off) + 치트 콘솔의 "navmesh" 명령으로 on/off.
    //
    // 데이터 소스: NavMeshService.Current(로드된 스테이지의 NavMesh). 스테이지가 바뀌면 자동으로 다시 빌드하고,
    // 언로드되면 지운다. 아직 로드 전에 켜두면 로드되는 즉시 그려진다.
    public class NavMeshDebugView : MonoBehaviour
    {
        // 바닥과 z-fighting 안 나도록 살짝 띄우는 높이.
        private const float k_yLift = 0.05f;

        // 갈 수 있는 영역(navmesh 폴리곤) 채움색: 연한 반투명 하늘색.
        private static readonly Color k_fillColor = new Color(0.35f, 0.75f, 1f, 0.22f);
        // 폴리곤 가장자리 선색: 또렷한 하늘색.
        private static readonly Color k_edgeColor = new Color(0f, 0.8f, 1f, 0.9f);

        // 표시 토글. 기본 꺼짐. 치트 콘솔의 "navmesh" 로 켜고 끈다.
        private static bool s_enabled;

        private static NavMeshDebugView sm_instance;

        private MeshFilter m_meshFilter;
        private Material m_fillMaterial;
        private Material m_edgeMaterial;
        private Mesh m_mesh;

        // 현재 메시를 빌드한 스테이지 이름. NavMeshService 의 스테이지가 바뀌면 다시 빌드하기 위한 변경 감지용.
        private string m_builtStage;

        public static bool IsEnabled => s_enabled;

        // 치트 토글: on/off 명시.
        public static void SetEnabled(bool enabled)
        {
            s_enabled = enabled;
            if (enabled)
                ensureInstance();
            if (sm_instance != null)
                sm_instance.apply();
        }

        // 치트 토글: 현재 상태를 뒤집고 결과(켜짐 여부)를 반환.
        public static bool Toggle()
        {
            SetEnabled(!s_enabled);
            return s_enabled;
        }

        private static void ensureInstance()
        {
            if (sm_instance != null)
                return;
            var go = new GameObject("[NavMeshDebugView]");
            go.AddComponent<NavMeshDebugView>();   // Awake 에서 sm_instance 세팅
        }

        private void Awake()
        {
            if (sm_instance != null && sm_instance != this)
            {
                Destroy(gameObject);
                return;
            }
            sm_instance = this;
            // 스테이지 이동(씬 재로드)에도 토글 상태를 유지하기 위해 파괴되지 않게 둔다.
            DontDestroyOnLoad(gameObject);

            m_meshFilter = gameObject.AddComponent<MeshFilter>();
            var meshRenderer = gameObject.AddComponent<MeshRenderer>();
            meshRenderer.shadowCastingMode = ShadowCastingMode.Off;
            meshRenderer.receiveShadows = false;

            // 채움(반투명) + 가장자리(불투명) 2개 머티리얼.
            // 메시의 서브메시 0 = 채움 삼각형, 서브메시 1 = 가장자리 선 (materials 순서와 일치).
            m_fillMaterial = makeMaterial(k_fillColor, transparent: true);
            m_edgeMaterial = makeMaterial(k_edgeColor, transparent: false);
            meshRenderer.materials = new[] { m_fillMaterial, m_edgeMaterial };
        }

        private void OnDestroy()
        {
            if (sm_instance == this)
                sm_instance = null;
        }

        private void Update()
        {
            if (!s_enabled)
                return;

            // 로드된 스테이지(NavMesh)가 바뀌면 다시 빌드, 언로드되면 지운다.
            if (NavMeshService.CurrentStageName != m_builtStage)
                apply();
        }

        // 현재 토글/로드 상태에 맞춰 메시를 다시 만들거나 지운다.
        private void apply()
        {
            if (!s_enabled || !NavMeshService.IsLoaded)
            {
                clear();
                // 꺼졌으면 다음에 켤 때 반드시 다시 빌드되도록 null 로. 켜졌지만 미로드면 현재값(null)을 반영해 매프레임 재시도 방지.
                m_builtStage = s_enabled ? NavMeshService.CurrentStageName : null;
                return;
            }

            buildMesh(NavMeshService.Current.NavMesh);
            m_builtStage = NavMeshService.CurrentStageName;
        }

        private void clear()
        {
            if (m_mesh != null)
                m_mesh.Clear();
        }

        // 단색 머티리얼 생성. transparent=true 면 알파 블렌딩(반투명)으로 구성한다.
        // URP/Unlit 를 우선 쓰고, 없으면 Sprites/Default(기본적으로 알파 블렌딩) 로 폴백.
        private static Material makeMaterial(Color color, bool transparent)
        {
            Shader shader = Shader.Find("Universal Render Pipeline/Unlit");
            if (shader != null)
            {
                var mat = new Material(shader);
                mat.color = color;
                mat.SetColor("_BaseColor", color);
                if (transparent)
                {
                    // URP/Unlit 를 Transparent(알파 블렌드)로 전환.
                    mat.SetFloat("_Surface", 1f);   // 0=Opaque, 1=Transparent
                    mat.SetFloat("_Blend", 0f);     // 0=Alpha
                    mat.SetFloat("_SrcBlend", (float)BlendMode.SrcAlpha);
                    mat.SetFloat("_DstBlend", (float)BlendMode.OneMinusSrcAlpha);
                    mat.SetFloat("_ZWrite", 0f);
                    mat.SetFloat("_Cull", (float)CullMode.Off);   // 양면(위/아래 어디서 봐도 보이게)
                    mat.EnableKeyword("_SURFACE_TYPE_TRANSPARENT");
                    mat.renderQueue = (int)RenderQueue.Transparent;
                }
                return mat;
            }

            // 폴백: Sprites/Default 는 알파 블렌딩을 기본 지원.
            var fallback = new Material(Shader.Find("Sprites/Default"));
            fallback.color = color;
            fallback.SetColor("_Color", color);
            return fallback;
        }

        // 로드된 NavMesh 의 모든 폴리곤을 하나의 메시로 만든다.
        //   서브메시 0 = 채움(삼각형, 반투명) — "갈 수 있는 영역" 표시.
        //   서브메시 1 = 가장자리(선, 불투명) — 폴리곤 경계.
        // 폴리곤 정점은 폴리곤마다 한 번만 저장하고, 채움/가장자리가 인덱스로 공유한다.
        private void buildMesh(DtNavMesh navMesh)
        {
            if (navMesh == null)
            {
                clear();
                return;
            }

            var verts = new List<Vector3>();
            var fillTris = new List<int>();
            var edgeLines = new List<int>();

            int maxTiles = navMesh.GetMaxTiles();
            for (int i = 0; i < maxTiles; i++)
            {
                DtMeshTile tile = navMesh.GetTile(i);
                if (tile == null || tile.data == null || tile.data.header == null)
                    continue;

                var data = tile.data;
                var header = data.header;
                for (int p = 0; p < header.polyCount; p++)
                {
                    var poly = data.polys[p];
                    // off-mesh connection(점프/텔레포트 링크)은 면이 아니라 점-점 링크라 스킵.
                    if (poly.GetPolyType() == DtPolyTypes.DT_POLYTYPE_OFFMESH_CONNECTION)
                        continue;

                    int vc = poly.vertCount;
                    if (vc < 3)
                        continue;

                    int baseIdx = verts.Count;
                    for (int j = 0; j < vc; j++)
                    {
                        int vi = poly.verts[j] * 3;
                        verts.Add(new Vector3(data.verts[vi], data.verts[vi + 1] + k_yLift, data.verts[vi + 2]));
                    }

                    // 채움: 볼록 다각형을 삼각형 부채꼴(fan)로 분해. [0,1,2],[0,2,3],...
                    for (int j = 2; j < vc; j++)
                    {
                        fillTris.Add(baseIdx);
                        fillTris.Add(baseIdx + j - 1);
                        fillTris.Add(baseIdx + j);
                    }
                    // 가장자리: 각 변. 마지막 정점 -> 첫 정점으로 닫음.
                    for (int j = 0; j < vc; j++)
                    {
                        edgeLines.Add(baseIdx + j);
                        edgeLines.Add(baseIdx + (j + 1) % vc);
                    }
                }
            }

            if (m_mesh == null)
            {
                m_mesh = new Mesh { name = "NavMeshDebug" };
                m_meshFilter.mesh = m_mesh;
            }
            m_mesh.Clear();
            if (verts.Count == 0)
                return;

            m_mesh.indexFormat = (verts.Count > 65000)
                ? IndexFormat.UInt32
                : IndexFormat.UInt16;
            m_mesh.SetVertices(verts);
            m_mesh.subMeshCount = 2;
            m_mesh.SetIndices(fillTris.ToArray(), MeshTopology.Triangles, 0);   // 서브메시 0: 채움
            m_mesh.SetIndices(edgeLines.ToArray(), MeshTopology.Lines, 1);      // 서브메시 1: 가장자리
            m_mesh.RecalculateBounds();

            Debug.Log($"[NavMeshDebugView] built: stage={NavMeshService.CurrentStageName} fillTris={fillTris.Count / 3} edges={edgeLines.Count / 2}");
        }
    }
}
