#if UNITY_EDITOR || DEVELOPMENT_BUILD
using System;
using System.Collections.Generic;
using Client.Network;
using Client.Packet;
using GameData;
using UnityEngine;

namespace Client.Game
{
    // [치트] 화면 어디에서든 띄울 수 있는 토글형 치트 콘솔 (개발 빌드 전용).
    //
    // - 자기 자신을 부트스트랩한다(RuntimeInitializeOnLoadMethod). 씬/프리팹 세팅 불필요.
    //   어느 씬에서 Play 해도 자동 생성되고 DontDestroyOnLoad 로 유지된다.
    // - 토글: 백틱(`) 키. 닫기: Esc.
    // - 라우팅 규칙:
    //     입력한 명령 이름이 "클라치트"로 등록돼 있으면  -> 로컬 실행 (패킷 X)
    //     아니면                                         -> CheatReq 로 서버에 전송
    //   서버는 유효성을 판단해 CheatRes 로 결과 메시지를 돌려주고, 콘솔에 표시된다.
    //   (클라가 서버치트 목록을 따로 들 필요가 없다 — 중복 관리 제거.)
    //
    // UI 는 IMGUI(OnGUI) 다. Canvas/EventSystem/프리팹 없이 항상 최상단에 그려지는,
    // 개발용 콘솔에 가장 단순한 방식.
    public class CheatConsole : MonoBehaviour
    {
        // 콘솔이 열려 있는지. 게임 입력(이동/스킬 등)을 막고 싶은 곳에서 이 값을 참조하면 된다.
        public static bool IsOpen { get; private set; }

        private const int MaxLogLines = 12;

        private static CheatConsole s_instance;

        private bool m_open;
        private string m_input = "";
        private readonly List<string> m_log = new List<string>();

        // 클라치트 테이블: 이름 -> 로컬 실행. 새 클라치트는 여기에 한 줄 추가.
        private readonly Dictionary<string, Action<string[]>> m_clientCheats =
            new Dictionary<string, Action<string[]>>();

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
        private static void Bootstrap()
        {
            if (s_instance != null)
                return;

            GameObject go = new GameObject("[CheatConsole]");
            go.AddComponent<CheatConsole>();
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

            RegisterClientCheats();

            // 서버치트 응답(CheatRes) 수신 -> 콘솔 출력.
            // PacketDispatcher.Instance 는 항상 존재하고, NetworkManager 와의 바인딩은 GameBootstrap 이 한다.
            PacketDispatcher.Instance.Register<global::GamePacket.CheatRes>(
                global::Common.GamePacketId.CheatRes, OnCheatRes);

            Log("cheat console ready. `(backtick) 로 토글, help 입력.");
        }

        private void OnDestroy()
        {
            if (s_instance == this)
            {
                PacketDispatcher.Instance.Unregister(global::Common.GamePacketId.CheatRes);
                s_instance = null;
            }
        }

        // ─── 클라치트 등록 ───────────────────────────────────────────────
        private void RegisterClientCheats()
        {
            // 사용 가능한 클라치트/도움말.
            m_clientCheats["help"] = _ =>
            {
                Log("[client cheats] " + string.Join(", ", m_clientCheats.Keys));
                Log("그 외 명령은 서버로 전송됩니다 (예: ping).");
            };

            // 스테이지 이동: 기존 클라 경로(StageManager.RequestStageMove)를 그대로 사용.
            // usage: stage <stageDataKey> [serverId]
            //   serverId 생략/0 → 같은 게임서버 내 이동. 0이 아니면 해당 게임서버로 크로스서버 이동.
            m_clientCheats["stage"] = args =>
            {
                if (args.Length < 1)
                {
                    Log("usage: stage <stageDataKey> [serverId]");
                    return;
                }
                if (!int.TryParse(args[0], out int stageDataKey))
                {
                    Log("invalid stageDataKey: " + args[0]);
                    return;
                }
                // 두 번째 인자(서버ID)는 선택. 있으면 파싱, 없으면 0(같은 서버).
                int targetGameServerId = 0;
                if (args.Length >= 2 && !int.TryParse(args[1], out targetGameServerId))
                {
                    Log("invalid serverId: " + args[1]);
                    return;
                }
                if (StageManager.Instance == null)
                {
                    Log("StageManager not ready.");
                    return;
                }
                StageManager.Instance.RequestStageMove(stageDataKey, EStagePositionType.Default, targetGameServerId);
                Log($"[client] stage move -> stage={stageDataKey} server={targetGameServerId}");
            };

            // sector 격자 디버그 표시 토글. 기본 꺼짐.
            // usage: sectorgrid [on|off]   (인자 없으면 현재 상태를 뒤집음)
            m_clientCheats["sectorgrid"] = args =>
            {
                bool state;
                if (args.Length >= 1)
                {
                    string a = args[0].ToLowerInvariant();
                    if (a == "on" || a == "1" || a == "true") { SectorGridDebug.SetEnabled(true); state = true; }
                    else if (a == "off" || a == "0" || a == "false") { SectorGridDebug.SetEnabled(false); state = false; }
                    else { Log("usage: sectorgrid [on|off]"); return; }
                }
                else
                {
                    state = SectorGridDebug.Toggle();
                }
                Log($"[client] sector grid {(state ? "ON" : "OFF")}");
            };

            // NavMesh 폴리곤 디버그 표시 토글. 기본 꺼짐.
            // usage: navmesh [on|off]   (인자 없으면 현재 상태를 뒤집음)
            m_clientCheats["navmesh"] = args =>
            {
                bool state;
                if (args.Length >= 1)
                {
                    string a = args[0].ToLowerInvariant();
                    if (a == "on" || a == "1" || a == "true") { MMO.Client.Navigation.NavMeshDebugView.SetEnabled(true); state = true; }
                    else if (a == "off" || a == "0" || a == "false") { MMO.Client.Navigation.NavMeshDebugView.SetEnabled(false); state = false; }
                    else { Log("usage: navmesh [on|off]"); return; }
                }
                else
                {
                    state = MMO.Client.Navigation.NavMeshDebugView.Toggle();
                }
                Log($"[client] navmesh {(state ? "ON" : "OFF")}");
            };
        }

        // ─── 입력 라우팅 ─────────────────────────────────────────────────
        private void Submit(string line)
        {
            Log("> " + line);

            string[] tokens = line.Split(new[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries);
            if (tokens.Length == 0)
                return;

            string name = tokens[0];
            string[] args = new string[tokens.Length - 1];
            Array.Copy(tokens, 1, args, 0, args.Length);

            if (m_clientCheats.TryGetValue(name, out Action<string[]> clientCheat))
            {
                try { clientCheat(args); }
                catch (Exception e) { Log("error: " + e.Message); }
                return;
            }

            // 클라치트가 아니면 서버치트로 전송.
            SendServerCheat(name, args);
        }

        private void SendServerCheat(string name, string[] args)
        {
            if (NetworkManager.Instance == null || !NetworkManager.Instance.IsConnected)
            {
                Log("not connected — 서버치트를 보낼 수 없습니다.");
                return;
            }

            var req = new global::GamePacket.CheatReq { Name = name };
            req.Args.AddRange(args);
            NetworkManager.Instance.Send(global::Common.GamePacketId.CheatReq, req);
            Log($"[server] -> {name} {string.Join(" ", args)}");
        }

        private void OnCheatRes(global::GamePacket.CheatRes res)
        {
            Log((res.Success ? "[OK] " : "[FAIL] ") + res.Message);
        }

        // ─── 출력 로그 ───────────────────────────────────────────────────
        private void Log(string msg)
        {
            m_log.Add(msg);
            if (m_log.Count > MaxLogLines)
                m_log.RemoveRange(0, m_log.Count - MaxLogLines);
        }

        // ─── UI (IMGUI) ──────────────────────────────────────────────────
        private void OnGUI()
        {
            Event e = Event.current;

            // 토글 (열려있든 닫혀있든 항상 처리). 백틱이 입력창에 들어가지 않도록 이벤트 소비.
            if (e.type == EventType.KeyDown && e.keyCode == KeyCode.BackQuote)
            {
                m_open = !m_open;
                IsOpen = m_open;
                e.Use();
                return;
            }

            if (!m_open)
                return;

            if (e.type == EventType.KeyDown && e.keyCode == KeyCode.Escape)
            {
                m_open = false;
                IsOpen = false;
                e.Use();
                return;
            }

            float x = 24f;
            float width = Mathf.Min(Screen.width - x - 20f, 760f);
            float y = 10f;

            // 입력 줄.
            bool submit = e.type == EventType.KeyDown &&
                          (e.keyCode == KeyCode.Return || e.keyCode == KeyCode.KeypadEnter);

            GUI.Box(new Rect(x, y, width, 26f), GUIContent.none);
            GUI.SetNextControlName("cheat_input");
            m_input = GUI.TextField(new Rect(x + 4f, y + 3f, width - 8f, 20f), m_input);
            GUI.FocusControl("cheat_input");

            if (submit)
            {
                string line = m_input.Trim();
                m_input = "";
                e.Use();
                if (!string.IsNullOrEmpty(line))
                    Submit(line);
            }

            // 출력 로그.
            float logY = y + 30f;
            float logH = MaxLogLines * 18f + 8f;
            GUI.Box(new Rect(x, logY, width, logH), GUIContent.none);
            for (int i = 0; i < m_log.Count; i++)
            {
                GUI.Label(new Rect(x + 6f, logY + 4f + i * 18f, width - 12f, 18f), m_log[i]);
            }
        }
    }
}
#endif
