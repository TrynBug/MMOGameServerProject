#if UNITY_EDITOR || DEVELOPMENT_BUILD
using System;
using System.Collections.Generic;
using Client.Network;
using Client.Packet;
using GameData;
using UnityEngine;

namespace Client.Game
{
    // [디버그] 서버측 정보를 실시간으로 보는 개발용 HUD (개발 빌드 전용).
    //
    // CheatConsole 과 같은 패턴:
    //   - RuntimeInitializeOnLoadMethod 로 자기 부트스트랩(씬/프리팹 세팅 불필요), DontDestroyOnLoad.
    //   - IMGUI(OnGUI) 로 항상 최상단에 그린다.
    //   - 토글: F9.
    //
    // 구독은 별도 패킷 없이 기존 치트 경로를 쓴다. 켜고 끌 때 dbgstat/dbgmon 치트를 CheatReq 로 보낸다.
    //   - 스탯 보기  : dbgstat <objectId>  -> 서버가 DebugStatNtf 주기 push
    //   - 몬스터 위치: dbgmon on/off        -> 서버가 DebugMonsterPositionsNtf 주기 push
    //
    // 버프 보기는 새 패킷 없이 클라가 이미 보관 중인 ActorObject.Buffs(BuffHolder)를 그대로 표시한다.
    public class DebugHud : MonoBehaviour
    {
        private static DebugHud s_instance;

        private bool m_open;

        // 선택 대상 objectId. 기본은 로컬 플레이어.
        private long m_targetId;
        private string m_targetInput = "";

        // 구독 상태(이 HUD 가 보낸 치트와 동기). 패널을 닫거나 HUD 를 닫으면 해제한다.
        private bool m_statSub;
        private bool m_monSub;

        // 수신 캐시.
        private readonly Dictionary<EStat, double> m_statValues = new Dictionary<EStat, double>();
        private long m_statValuesObjectId;
        private global::GamePacket.DebugMonsterPositionsNtf m_monPositions;

        private Vector2 m_statScroll;

        // 몬스터 위치 큐브 표시 설정 (HUD 에서 조절).
        private Color m_cubeColor = Color.green;
        private float m_cubeSize = 1.5f;

        // 와이어프레임 선 드로잉용 1x1 흰색 텍스처 (지연 생성).
        private static Texture2D s_lineTex;

        // 큐브 12개 엣지 (코너 인덱스 쌍). 코너 i 의 비트: bit0=x, bit1=y, bit2=z.
        private static readonly int[,] k_cubeEdges =
        {
            {0,1},{2,3},{4,5},{6,7},   // x 방향
            {0,2},{1,3},{4,6},{5,7},   // y 방향
            {0,4},{1,5},{2,6},{3,7},   // z 방향
        };

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
        private static void Bootstrap()
        {
            if (s_instance != null)
                return;
            GameObject go = new GameObject("[DebugHud]");
            go.AddComponent<DebugHud>();
        }

        private void Awake()
        {
            if (s_instance != null && s_instance != this)
            {
                Destroy(gameObject);
                return;
            }
            s_instance = this;
            DontDestroyOnLoad(gameObject);

            PacketDispatcher.Instance.Register<global::GamePacket.DebugStatNtf>(
                global::Common.GamePacketId.DebugStatNtf, onDebugStat);
            PacketDispatcher.Instance.Register<global::GamePacket.DebugMonsterPositionsNtf>(
                global::Common.GamePacketId.DebugMonsterPositionsNtf, onDebugMonsterPositions);
        }

        private void OnDestroy()
        {
            if (s_instance == this)
            {
                PacketDispatcher.Instance.Unregister(global::Common.GamePacketId.DebugStatNtf);
                PacketDispatcher.Instance.Unregister(global::Common.GamePacketId.DebugMonsterPositionsNtf);
                s_instance = null;
            }
        }

        // ─── 패킷 수신 ───────────────────────────────────────────────
        private void onDebugStat(global::GamePacket.DebugStatNtf ntf)
        {
            // 현재 보고 있는 대상의 것만 반영(다른 대상 잔여 패킷 무시).
            if (ntf.ObjectId != m_targetId)
                return;
            m_statValues.Clear();
            m_statValuesObjectId = ntf.ObjectId;
            foreach (var e in ntf.Entries)
                m_statValues[(EStat)e.Stat] = e.Value;
        }

        private void onDebugMonsterPositions(global::GamePacket.DebugMonsterPositionsNtf ntf)
        {
            m_monPositions = ntf;
        }

        // ─── 구독 토글 ───────────────────────────────────────────────
        private void setStatSub(bool on)
        {
            m_statSub = on;
            sendCheat("dbgstat", on ? m_targetId.ToString() : "0");
            if (!on)
            {
                m_statValues.Clear();
                m_statValuesObjectId = 0;
            }
        }

        private void setMonSub(bool on)
        {
            m_monSub = on;
            sendCheat("dbgmon", on ? "on" : "off");
            if (!on)
                m_monPositions = null;
        }

        private void sendCheat(string name, params string[] args)
        {
            if (NetworkManager.Instance == null || !NetworkManager.Instance.IsConnected)
                return;
            var req = new global::GamePacket.CheatReq { Name = name };
            req.Args.AddRange(args);
            NetworkManager.Instance.Send(global::Common.GamePacketId.CheatReq, req);
        }

        private long localPlayerId()
        {
            StageManager sm = StageManager.Instance;
            return (sm != null && sm.LocalPlayer != null) ? sm.LocalPlayer.UserId : 0;
        }

        // ─── UI (IMGUI) ──────────────────────────────────────────────
        private void OnGUI()
        {
            Event e = Event.current;
            if (e.type == EventType.KeyDown && e.keyCode == KeyCode.F9)
            {
                toggle();
                e.Use();
                return;
            }

            if (m_monSub)
                drawMonsterOverlay();   // 오버레이는 패널이 닫혀 있어도 그린다.

            if (!m_open)
                return;

            drawPanel();
        }

        private void toggle()
        {
            m_open = !m_open;
            if (m_open && m_targetId == 0)
            {
                m_targetId = localPlayerId();
                m_targetInput = m_targetId.ToString();
            }
        }

        private void drawPanel()
        {
            // 박스를 먼저 그리고, 내용 영역은 테두리 안쪽으로 여백을 줘서 시작한다.
            // (BeginArea 에 box 스타일을 직접 주면 테두리가 내용 왼쪽을 덮어 잘려 보인다.)
            Rect outer = new Rect(24f, 48f, 384f, Screen.height - 96f);
            GUI.Box(outer, GUIContent.none);
            GUILayout.BeginArea(new Rect(outer.x + 12f, outer.y + 10f, outer.width - 24f, outer.height - 20f));

            GUILayout.Label("=== Debug HUD (F9) ===");

            // 대상 선택.
            GUILayout.BeginHorizontal();
            GUILayout.Label("target", GUILayout.Width(45f));
            m_targetInput = GUILayout.TextField(m_targetInput, GUILayout.Width(150f));
            if (GUILayout.Button("Set", GUILayout.Width(45f)))
                applyTargetInput();
            if (GUILayout.Button("Me", GUILayout.Width(40f)))
            {
                m_targetId = localPlayerId();
                m_targetInput = m_targetId.ToString();
                if (m_statSub) sendCheat("dbgstat", m_targetId.ToString());
            }
            GUILayout.EndHorizontal();

            // 토글.
            GUILayout.BeginHorizontal();
            bool stat = GUILayout.Toggle(m_statSub, " Stat");
            if (stat != m_statSub) setStatSub(stat);
            bool mon = GUILayout.Toggle(m_monSub, " MonsterPos");
            if (mon != m_monSub) setMonSub(mon);
            GUILayout.EndHorizontal();

            drawCubeControls();
            drawStatPanel();
            drawBuffPanel();

            GUILayout.EndArea();
        }

        // 몬스터 위치 큐브 색상/크기 조절 UI.
        private void drawCubeControls()
        {
            GUILayout.Label("[MonsterPos cube]");

            GUILayout.BeginHorizontal();
            GUILayout.Label("R", GUILayout.Width(14f)); m_cubeColor.r = GUILayout.HorizontalSlider(m_cubeColor.r, 0f, 1f);
            GUILayout.Label("G", GUILayout.Width(14f)); m_cubeColor.g = GUILayout.HorizontalSlider(m_cubeColor.g, 0f, 1f);
            GUILayout.Label("B", GUILayout.Width(14f)); m_cubeColor.b = GUILayout.HorizontalSlider(m_cubeColor.b, 0f, 1f);
            GUILayout.EndHorizontal();

            GUILayout.BeginHorizontal();
            if (GUILayout.Button("Green"))  m_cubeColor = Color.green;
            if (GUILayout.Button("Yellow")) m_cubeColor = Color.yellow;
            if (GUILayout.Button("Red"))    m_cubeColor = Color.red;
            if (GUILayout.Button("Cyan"))   m_cubeColor = Color.cyan;
            if (GUILayout.Button("White"))  m_cubeColor = Color.white;
            GUILayout.EndHorizontal();

            GUILayout.BeginHorizontal();
            GUILayout.Label($"size {m_cubeSize:0.0}", GUILayout.Width(70f));
            m_cubeSize = GUILayout.HorizontalSlider(m_cubeSize, 0.3f, 5f);
            GUILayout.EndHorizontal();
        }

        private void applyTargetInput()
        {
            if (long.TryParse(m_targetInput, out long id))
            {
                m_targetId = id;
                m_statValues.Clear();
                if (m_statSub) sendCheat("dbgstat", m_targetId.ToString());
            }
        }

        // ① 스탯 보기. EStat 전체를 순회하며 수신값(없으면 0)을 표시. 0 아닌 값은 강조.
        private void drawStatPanel()
        {
            GUILayout.Label(m_statSub
                ? $"[Stat] object {m_statValuesObjectId}"
                : "[Stat] (off)");

            if (!m_statSub)
                return;

            m_statScroll = GUILayout.BeginScrollView(m_statScroll, GUILayout.Height(220f));
            foreach (EStat stat in Enum.GetValues(typeof(EStat)))
            {
                if (stat == EStat.None || stat == EStat.Max)
                    continue;
                double v = m_statValues.TryGetValue(stat, out double got) ? got : 0.0;
                Color prev = GUI.color;
                GUI.color = (v != 0.0) ? Color.white : new Color(1f, 1f, 1f, 0.4f);
                GUILayout.Label($"{stat,-16} {v:0.###}");
                GUI.color = prev;
            }
            GUILayout.EndScrollView();
        }

        // ② 버프 보기. 새 패킷 없이 대상의 BuffHolder 를 그대로 표시.
        private void drawBuffPanel()
        {
            ActorObject actor = (StageManager.Instance != null) ? StageManager.Instance.FindActor(m_targetId) : null;
            if (actor == null)
            {
                GUILayout.Label("[Buff] (대상이 클라에 없음)");
                return;
            }

            GUILayout.Label($"[Buff] {actor.Buffs.Count}개");
            foreach (var kv in actor.Buffs.Buffs)
            {
                BuffHolder.Entry b = kv.Value;
                string remain = b.IsPermanent ? "∞" : $"{b.RemainSecondsNow():0.0}s";
                GUILayout.Label($"  key={b.BuffKey} x{b.StackCount}  {remain}");
            }
        }

        // ③ 몬스터 서버 위치 오버레이. 서버 sector 진실 vs 클라 보유 몬스터를 대조한다.
        private void drawMonsterOverlay()
        {
            if (m_monPositions == null)
                return;
            Camera cam = Camera.main;
            if (cam == null)
                return;

            StageManager sm = StageManager.Instance;

            foreach (var pos in m_monPositions.Monsters)
            {
                Vector3 foot = new Vector3(pos.PosX, pos.PosY, pos.PosZ);
                float half = m_cubeSize * 0.5f;
                // 큐브 중심: 서버가 준 위치(발밑)에서 절반만큼 위로 올려 몸을 감싼다.
                Vector3 center = foot + new Vector3(0f, half, 0f);

                // 클라가 이 몬스터를 들고 있는지 + 데스싱크 거리.
                bool clientHas = sm != null && sm.Monsters.TryGetValue(pos.ObjectId, out MonsterObject mo) && mo != null;
                Color color = m_cubeColor;
                string label = pos.ObjectId.ToString();
                if (!clientHas)
                {
                    color = Color.red;       // 서버엔 있는데 클라엔 없음 → 가시성/스폰 버그 신호(색 설정 무시).
                    label += " NO-CLIENT";
                }
                else
                {
                    float d = Vector3.Distance(foot, sm.Monsters[pos.ObjectId].transform.position);
                    label += $" Δ{d:0.0}";
                }

                if (!drawWireCube(cam, center, half, color))
                    continue;   // 카메라 뒤 → 라벨도 스킵.

                // 라벨: 큐브 위쪽에.
                Vector3 top = cam.WorldToScreenPoint(center + new Vector3(0f, half, 0f));
                if (top.z > 0f)
                {
                    Color prev = GUI.color;
                    GUI.color = color;
                    GUI.Label(new Rect(top.x - 40f, Screen.height - top.y - 24f, 180f, 18f), label);
                    GUI.color = prev;
                }
            }
        }

        // 월드 큐브의 12개 엣지를 화면에 투영해 선으로 그린다(내부 투명, 선만).
        // 중심이 카메라 뒤면 false (호출자가 라벨도 스킵).
        private bool drawWireCube(Camera cam, Vector3 center, float half, Color color)
        {
            if (cam.WorldToScreenPoint(center).z <= 0f)
                return false;

            var c = new Vector3[8];
            for (int i = 0; i < 8; i++)
            {
                Vector3 corner = center + new Vector3(
                    ((i & 1) != 0 ? half : -half),
                    ((i & 2) != 0 ? half : -half),
                    ((i & 4) != 0 ? half : -half));
                c[i] = cam.WorldToScreenPoint(corner);
            }

            for (int e = 0; e < k_cubeEdges.GetLength(0); e++)
            {
                Vector3 p0 = c[k_cubeEdges[e, 0]];
                Vector3 p1 = c[k_cubeEdges[e, 1]];
                if (p0.z <= 0f || p1.z <= 0f)
                    continue;   // 코너가 카메라 뒤면 그 엣지 스킵.
                drawLine2D(new Vector2(p0.x, Screen.height - p0.y),
                           new Vector2(p1.x, Screen.height - p1.y), color, 1.5f);
            }
            return true;
        }

        // IMGUI 좌표계에서 두 점을 잇는 선을 그린다 (1x1 텍스처를 회전·스케일).
        private static void drawLine2D(Vector2 a, Vector2 b, Color color, float width)
        {
            Matrix4x4 savedMatrix = GUI.matrix;
            Color savedColor = GUI.color;

            GUI.color = color;
            float angle = Mathf.Atan2(b.y - a.y, b.x - a.x) * Mathf.Rad2Deg;
            float length = Vector2.Distance(a, b);
            GUIUtility.RotateAroundPivot(angle, a);
            GUI.DrawTexture(new Rect(a.x, a.y - width * 0.5f, length, width), lineTex());

            GUI.matrix = savedMatrix;
            GUI.color = savedColor;
        }

        private static Texture2D lineTex()
        {
            if (s_lineTex == null)
            {
                s_lineTex = new Texture2D(1, 1);
                s_lineTex.SetPixel(0, 0, Color.white);
                s_lineTex.Apply();
            }
            return s_lineTex;
        }
    }
}
#endif
