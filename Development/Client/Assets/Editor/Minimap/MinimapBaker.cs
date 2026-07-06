// MinimapBaker — 스테이지 맵을 탑다운으로 캡처해 미니맵 배경 텍스처를 굽는 에디터 도구.
//
// 사용: 상단 메뉴 Tools > Minimap > Minimap Baker
//   - StreamingAssets/NavMesh 의 *.navmeta.json 을 스캔해 맵 목록을 자동 구성한다.
//   - 각 맵의 프리팹은 관례(Resources/Prefabs/Stages/{맵이름})로 매칭한다.
//   - 맵별 Bake / 전체 Bake 로 Resources/UI/Minimap/{맵이름}_map.png 를 생성(Sprite 임포트까지).
//
// 핵심: 월드 경계를 navmeta 의 bounds 에서 읽어 카메라를 프레이밍한다.
//       이 bounds 는 서버 StageLoadCompleteRes 의 World경계와 동일 소스이므로,
//       구운 텍스처가 런타임 UI_Minimap 의 좌표변환과 항상 정렬된다(정렬 실수 원천 차단).
using System.Collections.Generic;
using System.IO;
using UnityEditor;
using UnityEngine;

public class MinimapBakerWindow : EditorWindow
{
    // ── navmeta JSON 파싱용 ────────────────────────────────────
    [System.Serializable] public class NavBounds { public float min_x, min_z, max_x, max_z; }
    [System.Serializable] class NavMeta { public string navmesh_file; public NavBounds bounds; }

    public class MapEntry
    {
        public string name;                // 맵 이름 (예: SyntyForest)
        public NavBounds bounds;           // 월드 경계
        public string prefabResPath;       // Resources 상대 경로 (Prefabs/Stages/{name})
        public bool prefabExists;
        public bool outputExists;
        public float worldW => bounds.max_x - bounds.min_x;
        public float worldH => bounds.max_z - bounds.min_z;
    }

    const string k_navMetaDir = "Assets/StreamingAssets/NavMesh";
    const string k_outputDir  = "Assets/Resources/UI/Minimap";

    // ── 베이크 파라미터 ────────────────────────────────────────
    int m_resolution = 1024;                                  // 세로(짧은쪽) 픽셀. 가로는 맵 비율로 자동.
    float m_camHeight = 1200f;                                // 카메라 높이(맵 최고점보다 충분히 위)
    Color m_bgColor = new Color(0.05f, 0.06f, 0.04f, 1f);     // 맵 밖(원 밖) 영역 색
    float m_lightIntensity = 1.15f;
    Vector3 m_lightEuler = new Vector3(55f, -30f, 0f);

    List<MapEntry> m_maps = new List<MapEntry>();
    Vector2 m_scroll;
    string m_status = "";

    [MenuItem("Tools/Minimap/Minimap Baker")]
    static void Open()
    {
        var w = GetWindow<MinimapBakerWindow>("Minimap Baker");
        w.minSize = new Vector2(460f, 360f);
        w.Refresh();
        w.Show();
    }

    void OnEnable() { Refresh(); }

    // ── 맵 목록 스캔 ───────────────────────────────────────────
    public static List<MapEntry> ScanMaps()
    {
        var list = new List<MapEntry>();
        if (!Directory.Exists(k_navMetaDir))
            return list;

        foreach (string path in Directory.GetFiles(k_navMetaDir, "*.navmeta.json"))
        {
            NavMeta meta;
            try { meta = JsonUtility.FromJson<NavMeta>(File.ReadAllText(path)); }
            catch { continue; }
            if (meta == null || meta.bounds == null)
                continue;

            // 파일명: {name}.bin.navmeta.json → name 추출
            string fname = Path.GetFileName(path);
            string name = fname.Substring(0, fname.Length - ".navmeta.json".Length);
            if (name.EndsWith(".bin")) name = name.Substring(0, name.Length - 4);

            var e = new MapEntry
            {
                name = name,
                bounds = meta.bounds,
                prefabResPath = "Prefabs/Stages/" + name,
            };
            e.prefabExists = Resources.Load<GameObject>(e.prefabResPath) != null;
            e.outputExists = File.Exists(k_outputDir + "/" + name + "_map.png");
            list.Add(e);
        }
        return list;
    }

    void Refresh()
    {
        m_maps = ScanMaps();
    }

    // ── 베이크 ─────────────────────────────────────────────────
    // 지정 맵을 탑다운 캡처해 Resources/UI/Minimap/{name}_map.png 로 저장(Sprite 임포트 포함).
    // 성공/실패 메시지를 반환한다.
    public static string BakeMap(MapEntry e, int resolution, float camHeight, Color bg, float lightIntensity, Vector3 lightEuler)
    {
        var prefab = Resources.Load<GameObject>(e.prefabResPath);
        if (prefab == null)
            return $"[{e.name}] 실패: 프리팹 없음 (Resources/{e.prefabResPath})";

        float worldW = e.worldW, worldH = e.worldH;
        if (worldW <= 0f || worldH <= 0f)
            return $"[{e.name}] 실패: 경계 오류 (w={worldW}, h={worldH})";

        float cx = (e.bounds.min_x + e.bounds.max_x) * 0.5f;
        float cz = (e.bounds.min_z + e.bounds.max_z) * 0.5f;

        int H = Mathf.Clamp(resolution, 64, 4096);
        int W = Mathf.Clamp(Mathf.RoundToInt(resolution * worldW / worldH), 64, 4096);

        // 열린 씬의 환경(안개/조명/앰비언트)이 캡처에 섞이지 않도록 격리한다.
        //   - 안개가 켜진 씬에서 구우면 카메라 거리(카메라높이)만큼 맵이 안개에 묻혀 안개색만 찍힌다.
        //   - 씬 조명/앰비언트도 씬마다 달라 결과가 비결정적 → 임시로 끄고 결정적 값으로 렌더.
        //   - 갓 인스턴스화한 맵만 전용 레이어로 렌더(cullingMask)해서 씬에 이미 있는 맵/오브젝트와의
        //     중복 렌더(z-fighting)를 배제한다. (try/finally 로 환경을 반드시 원복)
        bool prevFog = RenderSettings.fog;
        UnityEngine.Rendering.AmbientMode prevAmbMode = RenderSettings.ambientMode;
        Color prevAmbLight = RenderSettings.ambientLight;
        float prevAmbInt = RenderSettings.ambientIntensity;
        Light[] sceneLights = Object.FindObjectsByType<Light>(FindObjectsSortMode.None);
        var disabledLights = new List<Light>();

        const int k_bakeLayer = 31;
        GameObject map = null, camGO = null, lightGO = null;
        Camera cam = null;
        RenderTexture rt = null;
        Texture2D tex = null;
        byte[] png = null;
        try
        {
            RenderSettings.fog = false;
            RenderSettings.ambientMode = UnityEngine.Rendering.AmbientMode.Flat;
            RenderSettings.ambientLight = new Color(0.5f, 0.5f, 0.5f, 1f);
            RenderSettings.ambientIntensity = 1f;
            foreach (Light l in sceneLights)
                if (l != null && l.enabled) { l.enabled = false; disabledLights.Add(l); }

            map = Object.Instantiate(prefab);
            map.hideFlags = HideFlags.HideAndDontSave;
            setLayerRecursive(map.transform, k_bakeLayer);

            lightGO = new GameObject("__mmLight") { hideFlags = HideFlags.HideAndDontSave };
            var light = lightGO.AddComponent<Light>();
            light.type = LightType.Directional;
            light.intensity = lightIntensity;
            lightGO.transform.rotation = Quaternion.Euler(lightEuler);

            camGO = new GameObject("__mmCam") { hideFlags = HideFlags.HideAndDontSave };
            cam = camGO.AddComponent<Camera>();
            cam.orthographic = true;
            cam.orthographicSize = worldH * 0.5f;      // 세로 절반
            cam.aspect = worldW / worldH;              // 가로 = worldW (맵 비율 그대로 → 왜곡 없음)
            cam.nearClipPlane = 0.3f;
            cam.farClipPlane = camHeight * 2f + 1000f;
            cam.clearFlags = CameraClearFlags.SolidColor;
            cam.backgroundColor = bg;
            cam.cullingMask = 1 << k_bakeLayer;        // 갓 만든 인스턴스만 렌더
            camGO.transform.position = new Vector3(cx, camHeight, cz);
            camGO.transform.rotation = Quaternion.Euler(90f, 0f, 0f);   // 수직 아래를 봄(+Z=위, +X=오른쪽)

            rt = new RenderTexture(W, H, 24, RenderTextureFormat.ARGB32);
            rt.Create();
            cam.targetTexture = rt;
            cam.Render();

            RenderTexture prevActive = RenderTexture.active;
            RenderTexture.active = rt;
            tex = new Texture2D(W, H, TextureFormat.RGBA32, false);
            tex.ReadPixels(new Rect(0, 0, W, H), 0, 0);
            tex.Apply();
            RenderTexture.active = prevActive;
            png = tex.EncodeToPNG();
        }
        finally
        {
            RenderSettings.fog = prevFog;
            RenderSettings.ambientMode = prevAmbMode;
            RenderSettings.ambientLight = prevAmbLight;
            RenderSettings.ambientIntensity = prevAmbInt;
            foreach (Light l in disabledLights) if (l != null) l.enabled = true;
            if (cam != null) cam.targetTexture = null;   // rt 해제 전에 카메라 타깃 해제(경고 방지)
            if (rt != null) rt.Release();
            if (tex != null) Object.DestroyImmediate(tex);
            if (rt != null) Object.DestroyImmediate(rt);
            if (map != null) Object.DestroyImmediate(map);
            if (camGO != null) Object.DestroyImmediate(camGO);
            if (lightGO != null) Object.DestroyImmediate(lightGO);
        }

        if (png == null || png.Length == 0)
            return $"[{e.name}] 실패: 렌더 결과 없음";

        Directory.CreateDirectory(k_outputDir);
        string outPath = k_outputDir + "/" + e.name + "_map.png";
        File.WriteAllBytes(outPath, png);
        AssetDatabase.ImportAsset(outPath, ImportAssetOptions.ForceUpdate);

        var imp = AssetImporter.GetAtPath(outPath) as TextureImporter;
        if (imp != null)
        {
            imp.textureType = TextureImporterType.Sprite;
            imp.spriteImportMode = SpriteImportMode.Single;
            imp.mipmapEnabled = false;
            imp.wrapMode = TextureWrapMode.Clamp;
            imp.filterMode = FilterMode.Bilinear;
            imp.alphaIsTransparency = true;
            imp.maxTextureSize = Mathf.Max(W, H) <= 1024 ? 1024 : 2048;
            imp.SaveAndReimport();
        }
        e.outputExists = true;
        return $"[{e.name}] OK  {W}x{H}  → {outPath}";
    }

    string BakeMap(MapEntry e) => BakeMap(e, m_resolution, m_camHeight, m_bgColor, m_lightIntensity, m_lightEuler);

    // GameObject 와 모든 자식의 layer 를 지정 값으로 설정한다(베이크 전용 레이어 격리용).
    static void setLayerRecursive(Transform t, int layer)
    {
        t.gameObject.layer = layer;
        for (int i = 0; i < t.childCount; i++)
            setLayerRecursive(t.GetChild(i), layer);
    }

    // ── GUI ────────────────────────────────────────────────────
    void OnGUI()
    {
        EditorGUILayout.Space(4f);
        using (new EditorGUILayout.HorizontalScope())
        {
            EditorGUILayout.LabelField("미니맵 배경 텍스처 베이커", EditorStyles.boldLabel);
            GUILayout.FlexibleSpace();
            if (GUILayout.Button("새로고침", GUILayout.Width(80f)))
                Refresh();
        }
        EditorGUILayout.HelpBox(
            "StreamingAssets/NavMesh 의 navmeta 경계로 맵을 탑다운 캡처합니다.\n" +
            "출력: Resources/UI/Minimap/{맵이름}_map.png (Sprite)", MessageType.None);

        EditorGUILayout.Space(4f);
        EditorGUILayout.LabelField("파라미터", EditorStyles.boldLabel);
        m_resolution = EditorGUILayout.IntPopup("해상도(짧은변)", m_resolution,
            new[] { "512", "1024", "2048" }, new[] { 512, 1024, 2048 });
        m_camHeight = EditorGUILayout.FloatField("카메라 높이", m_camHeight);
        m_bgColor = EditorGUILayout.ColorField("배경색(맵 밖)", m_bgColor);
        m_lightIntensity = EditorGUILayout.Slider("라이트 강도", m_lightIntensity, 0f, 3f);
        m_lightEuler = EditorGUILayout.Vector3Field("라이트 각도", m_lightEuler);

        EditorGUILayout.Space(6f);
        using (new EditorGUILayout.HorizontalScope())
        {
            EditorGUILayout.LabelField($"맵 ({m_maps.Count})", EditorStyles.boldLabel);
            GUILayout.FlexibleSpace();
            using (new EditorGUI.DisabledScope(m_maps.Count == 0))
                if (GUILayout.Button("전체 Bake", GUILayout.Width(100f)))
                    BakeAll();
        }

        if (m_maps.Count == 0)
        {
            EditorGUILayout.HelpBox($"navmeta 를 찾지 못했습니다. ({k_navMetaDir}/*.navmeta.json)", MessageType.Warning);
        }
        else
        {
            m_scroll = EditorGUILayout.BeginScrollView(m_scroll);
            foreach (var e in m_maps)
            {
                using (new EditorGUILayout.HorizontalScope(EditorStyles.helpBox))
                {
                    using (new EditorGUILayout.VerticalScope())
                    {
                        EditorGUILayout.LabelField(e.name, EditorStyles.boldLabel);
                        EditorGUILayout.LabelField(
                            $"월드 {e.worldW:0.#}×{e.worldH:0.#}   " +
                            $"프리팹 {(e.prefabExists ? "✓" : "✗")}   " +
                            $"출력 {(e.outputExists ? "✓" : "—")}",
                            EditorStyles.miniLabel);
                    }
                    GUILayout.FlexibleSpace();
                    using (new EditorGUI.DisabledScope(!e.prefabExists))
                        if (GUILayout.Button(e.outputExists ? "다시 Bake" : "Bake", GUILayout.Width(90f), GUILayout.Height(34f)))
                            RunBake(e);
                }
            }
            EditorGUILayout.EndScrollView();
        }

        if (!string.IsNullOrEmpty(m_status))
        {
            EditorGUILayout.Space(4f);
            EditorGUILayout.HelpBox(m_status, MessageType.Info);
        }
    }

    void RunBake(MapEntry e)
    {
        m_status = BakeMap(e);
        Repaint();
    }

    void BakeAll()
    {
        var sb = new System.Text.StringBuilder();
        int ok = 0;
        foreach (var e in m_maps)
        {
            if (!e.prefabExists) { sb.AppendLine($"[{e.name}] 건너뜀: 프리팹 없음"); continue; }
            string r = BakeMap(e);
            sb.AppendLine(r);
            if (r.Contains("OK")) ok++;
        }
        m_status = $"전체 Bake 완료: {ok}/{m_maps.Count}\n" + sb.ToString().TrimEnd();
        Repaint();
    }
}
