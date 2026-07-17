// PrefabPlacerTool — 프리팹을 원하는 위치/간격으로 빠르게 배치하는 에디터 도구.
//
// 사용: 상단 메뉴 Tools > Level > Prefab Placer
//   - Paint 탭: 씬뷰에서 클릭/드래그로 표면 위에 배치 (원하는 위치)
//   - Array 탭: 라인/그리드로 일정 간격 일괄 배치 (원하는 간격)
// 공통: 표면 정렬, 그리드 스냅, 랜덤 Y회전/스케일/위치지터, 부모지정, Undo 지원.
using System.Collections.Generic;
using UnityEditor;
using UnityEngine;

public class PrefabPlacerTool : EditorWindow
{
    // ── 프리팹 ────────────────────────────────────────────────
    [SerializeField] List<GameObject> prefabs = new List<GameObject>();
    enum PickMode { Sequential, Random }
    PickMode pickMode = PickMode.Random;
    int seqIndex = 0;

    [SerializeField] Transform parent;

    // ── 탭 ────────────────────────────────────────────────────
    enum Tab { Paint, Array }
    Tab tab = Tab.Paint;

    // ── Array 파라미터 ─────────────────────────────────────────
    enum ArrayShape { Line, Grid }
    ArrayShape shape = ArrayShape.Grid;
    Vector3 origin = Vector3.zero;
    int countX = 5, countZ = 5;
    Vector2 spacing = new Vector2(4, 4);

    // ── Paint 파라미터 ─────────────────────────────────────────
    bool painting = false;
    float paintSpacing = 2f;
    Vector3 lastPaintPos;
    bool hasLastPaint = false;

    // ── 공통 옵션 ─────────────────────────────────────────────
    bool snapToGrid = false;
    float gridSize = 1f;
    bool alignToSurface = true;
    float surfaceOffset = 0f;
    Vector3 baseEuler = Vector3.zero;
    Vector2 randomYRot = new Vector2(0, 0);
    Vector2 randomScale = new Vector2(1, 1);
    float positionJitter = 0f;

    Vector2 scroll;

    [MenuItem("Tools/Level/Prefab Placer")]
    static void Open() => GetWindow<PrefabPlacerTool>("Prefab Placer");

    void OnEnable()
    {
        if (prefabs.Count == 0) prefabs.Add(null);
        SceneView.duringSceneGui += OnSceneGUI;
    }
    void OnDisable()
    {
        SceneView.duringSceneGui -= OnSceneGUI;
        painting = false;
    }

    // ════════════════════════════════════════════════════════════
    //  인스펙터 UI
    // ════════════════════════════════════════════════════════════
    void OnGUI()
    {
        scroll = EditorGUILayout.BeginScrollView(scroll);

        EditorGUILayout.LabelField("Prefabs", EditorStyles.boldLabel);
        for (int i = 0; i < prefabs.Count; i++)
        {
            EditorGUILayout.BeginHorizontal();
            prefabs[i] = (GameObject)EditorGUILayout.ObjectField(prefabs[i], typeof(GameObject), false);
            if (GUILayout.Button("−", GUILayout.Width(24)) && prefabs.Count > 1)
            { prefabs.RemoveAt(i); i--; }
            EditorGUILayout.EndHorizontal();
        }
        EditorGUILayout.BeginHorizontal();
        if (GUILayout.Button("+ Add prefab")) prefabs.Add(null);
        if (prefabs.Count > 1) pickMode = (PickMode)EditorGUILayout.EnumPopup(pickMode, GUILayout.Width(110));
        EditorGUILayout.EndHorizontal();

        parent = (Transform)EditorGUILayout.ObjectField("Parent", parent, typeof(Transform), true);

        EditorGUILayout.Space();
        tab = (Tab)GUILayout.Toolbar((int)tab, new[] { "Paint (씬뷰 클릭)", "Array (간격 배치)" });
        EditorGUILayout.Space();

        if (tab == Tab.Paint) DrawPaintTab();
        else DrawArrayTab();

        EditorGUILayout.Space();
        DrawCommonOptions();

        EditorGUILayout.EndScrollView();

        if (Event.current.type == EventType.MouseMove) Repaint();
    }

    void DrawPaintTab()
    {
        EditorGUILayout.HelpBox(
            "씬뷰에서 좌클릭/드래그로 표면 위에 배치합니다.\n" +
            "Paint Spacing 보다 가까운 곳에는 중복 배치되지 않습니다.",
            MessageType.Info);
        paintSpacing = Mathf.Max(0.01f, EditorGUILayout.FloatField("Paint Spacing", paintSpacing));

        GUI.backgroundColor = painting ? new Color(1f, 0.5f, 0.5f) : Color.white;
        if (GUILayout.Button(painting ? "■ Stop Painting" : "● Start Painting", GUILayout.Height(28)))
        {
            painting = !painting;
            hasLastPaint = false;
            if (painting && !HasValidPrefab())
            { painting = false; ShowNotification(new GUIContent("프리팹을 먼저 지정하세요")); }
            SceneView.RepaintAll();
        }
        GUI.backgroundColor = Color.white;
    }

    void DrawArrayTab()
    {
        shape = (ArrayShape)EditorGUILayout.EnumPopup("Shape", shape);
        EditorGUILayout.BeginHorizontal();
        origin = EditorGUILayout.Vector3Field("Origin", origin);
        EditorGUILayout.EndHorizontal();
        if (GUILayout.Button("Origin ← 선택 오브젝트 위치"))
        {
            if (Selection.activeTransform != null) origin = Selection.activeTransform.position;
        }

        countX = Mathf.Max(1, EditorGUILayout.IntField(shape == ArrayShape.Line ? "Count" : "Count X", countX));
        if (shape == ArrayShape.Grid)
            countZ = Mathf.Max(1, EditorGUILayout.IntField("Count Z", countZ));
        EditorGUILayout.BeginHorizontal();
        EditorGUILayout.PrefixLabel("Spacing");
        spacing.x = EditorGUILayout.FloatField("X", spacing.x);
        spacing.y = EditorGUILayout.FloatField("Z", spacing.y);
        EditorGUILayout.EndHorizontal();

        int total = shape == ArrayShape.Line ? countX : countX * countZ;
        using (new EditorGUI.DisabledScope(!HasValidPrefab()))
            if (GUILayout.Button($"Generate ({total}개)", GUILayout.Height(26)))
                GenerateArray();
    }

    void DrawCommonOptions()
    {
        EditorGUILayout.LabelField("배치 옵션", EditorStyles.boldLabel);

        baseEuler = EditorGUILayout.Vector3Field("Rotation (XYZ°)", baseEuler);
        if (GUILayout.Button("Rotation ← 선택 오브젝트") && Selection.activeTransform != null)
            baseEuler = Selection.activeTransform.eulerAngles;

        alignToSurface = EditorGUILayout.Toggle("Align To Surface (노멀 정렬)", alignToSurface);
        surfaceOffset = EditorGUILayout.FloatField("Surface Offset", surfaceOffset);

        snapToGrid = EditorGUILayout.Toggle("Snap To Grid", snapToGrid);
        if (snapToGrid) gridSize = Mathf.Max(0.01f, EditorGUILayout.FloatField("Grid Size", gridSize));

        randomYRot = EditorGUILayout.Vector2Field("Random Y Rot (min,max°)", randomYRot);
        randomScale = EditorGUILayout.Vector2Field("Random Scale (min,max)", randomScale);
        positionJitter = Mathf.Max(0f, EditorGUILayout.FloatField("Position Jitter", positionJitter));
    }

    // ════════════════════════════════════════════════════════════
    //  씬뷰 GUI (페인팅 + 미리보기)
    // ════════════════════════════════════════════════════════════
    void OnSceneGUI(SceneView sv)
    {
        if (tab == Tab.Array) { DrawArrayPreview(); return; }
        if (!painting) return;

        Event e = Event.current;
        int id = GUIUtility.GetControlID(FocusType.Passive);
        HandleUtility.AddDefaultControl(id); // 클릭 시 오브젝트 선택 막기

        if (!TryGetSurface(e.mousePosition, out Vector3 pos, out Vector3 normal)) return;
        Vector3 placePos = ApplyGridSnap(pos);

        // 커서 미리보기
        Handles.color = new Color(0.2f, 1f, 0.4f, 1f);
        Handles.DrawWireDisc(placePos, normal, Mathf.Max(0.25f, paintSpacing * 0.5f));
        Handles.DrawLine(placePos, placePos + normal * 1.5f);
        SceneView.RepaintAll();

        bool paintEvent = (e.type == EventType.MouseDown || e.type == EventType.MouseDrag)
                          && e.button == 0 && !e.alt;
        if (paintEvent)
        {
            if (!hasLastPaint || Vector3.Distance(placePos, lastPaintPos) >= paintSpacing)
            {
                PlaceOne(placePos, normal);
                lastPaintPos = placePos;
                hasLastPaint = true;
            }
            e.Use();
        }
        if (e.type == EventType.MouseUp && e.button == 0) hasLastPaint = false;
    }

    void DrawArrayPreview()
    {
        if (!HasValidPrefab()) return;
        Handles.color = new Color(0.3f, 0.7f, 1f, 0.9f);
        foreach (var p in ArrayPositions())
            Handles.DrawWireCube(p, new Vector3(spacing.x, 0.1f, spacing.y) * 0.9f);
    }

    // ════════════════════════════════════════════════════════════
    //  배치 로직
    // ════════════════════════════════════════════════════════════
    IEnumerable<Vector3> ArrayPositions()
    {
        if (shape == ArrayShape.Line)
        {
            // X·Z 둘 다 방향 성분으로 사용 → X축/Z축/대각선 라인 모두 가능
            for (int i = 0; i < countX; i++)
                yield return origin + new Vector3(i * spacing.x, 0, i * spacing.y);
        }
        else
        {
            for (int x = 0; x < countX; x++)
                for (int z = 0; z < countZ; z++)
                    yield return origin + new Vector3(x * spacing.x, 0, z * spacing.y);
        }
    }

    void GenerateArray()
    {
        int group = Undo.GetCurrentGroup();
        foreach (var basePos in ArrayPositions())
        {
            Vector3 pos = basePos;
            Vector3 normal = Vector3.up;
            // 콜라이더가 있으면 위에서 아래로 레이캐스트해 지면에 안착
            if (alignToSurface &&
                Physics.Raycast(basePos + Vector3.up * 1000f, Vector3.down, out RaycastHit hit, 5000f))
            { pos = hit.point; normal = hit.normal; }
            PlaceOne(ApplyGridSnap(pos), normal);
        }
        Undo.SetCurrentGroupName("Prefab Placer: Generate Array");
        Undo.CollapseUndoOperations(group);
    }

    GameObject PlaceOne(Vector3 pos, Vector3 normal)
    {
        var prefab = NextPrefab();
        if (prefab == null) return null;

        if (positionJitter > 0f)
        {
            Vector2 j = Random.insideUnitCircle * positionJitter;
            pos += new Vector3(j.x, 0, j.y);
        }
        pos += (alignToSurface ? normal : Vector3.up) * surfaceOffset;

        Quaternion rot = Quaternion.Euler(baseEuler);
        rot *= Quaternion.Euler(0, Random.Range(randomYRot.x, randomYRot.y), 0);
        if (alignToSurface) rot = Quaternion.FromToRotation(Vector3.up, normal) * rot;

        var inst = (GameObject)PrefabUtility.InstantiatePrefab(prefab, parent);
        inst.transform.SetPositionAndRotation(pos, rot);
        float s = Random.Range(randomScale.x, randomScale.y);
        if (!Mathf.Approximately(s, 1f)) inst.transform.localScale *= s;

        Undo.RegisterCreatedObjectUndo(inst, "Place Prefab");
        return inst;
    }

    // ── 헬퍼 ──────────────────────────────────────────────────
    bool HasValidPrefab()
    {
        foreach (var p in prefabs) if (p != null) return true;
        return false;
    }

    GameObject NextPrefab()
    {
        var valid = new List<GameObject>();
        foreach (var p in prefabs) if (p != null) valid.Add(p);
        if (valid.Count == 0) return null;
        if (pickMode == PickMode.Random || valid.Count == 1)
            return valid[Random.Range(0, valid.Count)];
        var pick = valid[seqIndex % valid.Count];
        seqIndex++;
        return pick;
    }

    Vector3 ApplyGridSnap(Vector3 v)
    {
        if (!snapToGrid) return v;
        v.x = Mathf.Round(v.x / gridSize) * gridSize;
        v.z = Mathf.Round(v.z / gridSize) * gridSize;
        return v;
    }

    // 씬 지오메트리(콜라이더 없어도 됨) 위 지점/노멀을 구한다. 실패 시 Y=0 평면.
    bool TryGetSurface(Vector2 guiPos, out Vector3 pos, out Vector3 normal)
    {
        if (HandleUtility.PlaceObject(guiPos, out pos, out normal))
            return true;
        Ray ray = HandleUtility.GUIPointToWorldRay(guiPos);
        Plane ground = new Plane(Vector3.up, Vector3.zero);
        if (ground.Raycast(ray, out float d))
        {
            pos = ray.GetPoint(d);
            normal = Vector3.up;
            return true;
        }
        normal = Vector3.up;
        return false;
    }
}
