using System;
using System.Threading.Tasks;
using Client.Managers;
using Client.Network;
using Client.Packet;
using Client.UI;
using Common;
using GamePacket;
using UnityEngine;

namespace Client.Game
{
    // 로그인 씬의 시퀀스 컨트롤러.
    //
    // 책임:
    //   - UI_Login 을 띄우고 이벤트 구독 (로그인 버튼 클릭)
    //   - 네트워크 시퀀스 진행 (로그인 → 게이트웨이 → 캐릭터 목록)
    //   - 진행 상태를 UI 에 표시
    //   - 성공 시 씬 전환
    //
    // 책임이 아닌 것:
    //   - UI 의 레이아웃, 자식 컴포넌트 직접 참조 (UI_Login 이 캡슐화)
    //   - UI 생성 (UIManager 가 담당)
    //
    // 시퀀스:
    //   1. 로그인서버 connect
    //   2. LoginReq → LoginRes
    //   3. 로그인서버 disconnect
    //   4. 게이트웨이서버 connect
    //   5. GatewayAuthReq → GameEnterNtf
    //   6. CharacterListNtf 수신 → 캐시 저장
    //   7. CharacterSelection 씬으로 무조건 이동
    public class LoginSceneController : MonoBehaviour
    {
        [Header("Login Server")]
        [SerializeField] private string m_loginIp = "127.0.0.1";
        [SerializeField] private int m_loginPort = 8001;

        [Header("Timeouts (seconds)")]
        [SerializeField] private float m_connectTimeoutSec = 10f;
        [SerializeField] private float m_packetTimeoutSec = 10f;

        private NetworkManager m_net;
        private UI_Login m_uiLogin;

        // 패킷 수신을 await 가능한 형태로 만들기 위한 TaskCompletionSource.
        private TaskCompletionSource<LoginRes> m_tcsLoginRes;
        private TaskCompletionSource<GameEnterNtf> m_tcsGameEnter;
        private TaskCompletionSource<CharacterListNtf> m_tcsCharacterList;

        // 연결 이벤트(OnConnected/OnDisconnected)를 await하기 위한 TCS
        private TaskCompletionSource<bool> m_tcsConnected;

        private bool m_isLoggingIn;

        // ─── Unity 라이프사이클 ──────────────────────────────────────────

        private void Start()
        {
            m_net = NetworkManager.Instance;
            if (m_net == null)
            {
                Debug.LogError("[LoginScene] NetworkManager.Instance 가 없습니다. GameBootstrap이 실행되었는지 확인하세요.");
                return;
            }

            // UI 띄우기. UIManager 가 Resources/UI/Scene/UI_Login 프리팹 로드.
            m_uiLogin = Managers.Managers.UI.ShowSceneUI<UI_Login>();
            if (m_uiLogin == null)
            {
                Debug.LogError("[LoginScene] UI_Login 프리팹을 로드하지 못했습니다. Resources/UI/Scene/UI_Login.prefab 가 있는지 확인하세요.");
                return;
            }

            // UI 이벤트 구독
            m_uiLogin.OnLoginClicked += onLoginButtonClicked;

            // 네트워크 이벤트 구독
            m_net.OnConnected += onConnected;
            m_net.OnDisconnected += onDisconnected;

            // 패킷 핸들러 등록
            PacketDispatcher.Instance.Register<LoginRes>(GamePacketId.LoginRes, onLoginRes);
            PacketDispatcher.Instance.Register<GameEnterNtf>(GamePacketId.GameEnterNtf, onGameEnterNtf);
            PacketDispatcher.Instance.Register<CharacterListNtf>(GamePacketId.CharacterListNtf, onCharacterListNtf);
            PacketDispatcher.Instance.Register<ForceDisconnectNtf>(GamePacketId.ForceDisconnectNtf, onForceDisconnectNtf);

            m_uiLogin.SetStatus("ID와 비밀번호를 입력하세요");
        }

        private void OnDestroy()
        {
            if (m_net != null)
            {
                m_net.OnConnected -= onConnected;
                m_net.OnDisconnected -= onDisconnected;
            }

            if (m_uiLogin != null)
            {
                m_uiLogin.OnLoginClicked -= onLoginButtonClicked;
            }

            // 패킷 핸들러는 여기서 풀지 않는다.
            // Register는 같은 ID에 덮어쓰기 가능하므로 leak이 아니고,
            // StageManager.Awake가 다시 자기 핸들러로 덮어쓴다.
        }

        // ─── UI 이벤트 ──────────────────────────────────────────────────

        private void onLoginButtonClicked()
        {
            if (m_isLoggingIn) return;

            string id = m_uiLogin.GetId();
            string pw = m_uiLogin.GetPassword();

            if (string.IsNullOrEmpty(id))
            {
                m_uiLogin.SetStatus("ID를 입력하세요");
                return;
            }
            if (string.IsNullOrEmpty(pw))
            {
                m_uiLogin.SetStatus("비밀번호를 입력하세요");
                return;
            }

            // async void 핸들러를 직접 만들지 않고 별도 메서드 호출.
            // 예외는 runLoginFlow 내부에서 모두 잡힘.
            _ = runLoginFlow(id, pw);
        }

        // ─── 메인 시퀀스 ────────────────────────────────────────────────

        private async Task runLoginFlow(string id, string pw)
        {
            m_isLoggingIn = true;
            m_uiLogin.SetLoginButtonInteractable(false);

            try
            {
                // 1. 로그인서버 connect
                m_uiLogin.SetStatus("로그인서버 접속 중...");
                if (!await connectAsync(m_loginIp, m_loginPort))
                {
                    fail("로그인서버 접속 실패");
                    return;
                }

                // 2. LoginReq 송신 → LoginRes 대기
                m_uiLogin.SetStatus("로그인 중...");
                LoginRes loginRes = await sendAndAwaitLoginAsync(id, pw);
                if (loginRes == null)
                {
                    fail("로그인 응답 시간 초과 또는 연결 끊김");
                    return;
                }
                if (!loginRes.Success)
                {
                    fail($"로그인 실패: {loginRes.ErrorMsg}");
                    return;
                }

                Debug.Log($"[LoginScene] LoginRes OK. accountId={loginRes.AccountId}, gateway={loginRes.GatewayIp}:{loginRes.GatewayPort}");

                long accountId = loginRes.AccountId;
                ulong authToken = loginRes.AuthToken;
                string gatewayIp = loginRes.GatewayIp;
                int gatewayPort = loginRes.GatewayPort;

                // 3. 로그인서버 끊기
                m_net.Disconnect();
                // OnDisconnected가 한번 호출되며 정상 종료로 처리된다.
                // 다음 connect 전에 한 프레임 정도 여유를 두는 게 안전.
                await Task.Yield();

                // 4. 게이트웨이서버 connect
                m_uiLogin.SetStatus("게이트웨이 접속 중...");
                if (!await connectAsync(gatewayIp, gatewayPort))
                {
                    fail("게이트웨이 접속 실패");
                    return;
                }

                // 5. GatewayAuthReq 송신 (응답 패킷 없음, 다음에 GameEnterNtf와 CharacterListNtf가 연달아 옴)
                // 두 TCS를 모두 미리 만들어둔다. 서버가 두 패킷을 거의 동시에 보내기 때문에
                // CharacterListNtf가 GameEnterNtf await 완료 전에 도착하면 패킷이 버려진다 (race condition).
                m_uiLogin.SetStatus("인증 중...");
                m_tcsGameEnter = newTcs<GameEnterNtf>();
                m_tcsCharacterList = newTcs<CharacterListNtf>();
                GatewayAuthReq authReq = new GatewayAuthReq
                {
                    AccountId = accountId,
                    AuthToken = authToken
                };
                m_net.Send(GamePacketId.GatewayAuthReq, authReq);

                // 6. GameEnterNtf 수신 대기
                GameEnterNtf gameEnter = await awaitWithTimeout(m_tcsGameEnter.Task, m_packetTimeoutSec);
                if (gameEnter == null)
                {
                    fail("게임서버 입장 응답 없음 (인증 실패 또는 연결 끊김)");
                    return;
                }
                Debug.Log($"[LoginScene] GameEnterNtf received. stage={gameEnter.StageId}");

                // 7. CharacterListNtf 수신 대기 (이미 도착해 있을 수도 있고, 곧 도착할 수도 있음)
                m_uiLogin.SetStatus("캐릭터 정보 로딩 중...");
                CharacterListNtf charList = await awaitWithTimeout(m_tcsCharacterList.Task, m_packetTimeoutSec);
                if (charList == null)
                {
                    fail("캐릭터 목록 수신 실패");
                    return;
                }

                // 캐시에 저장
                CharacterDataCache.Instance.SetAccountId(accountId);
                CharacterDataCache.Instance.SetCharacters(charList.Characters);

                Debug.Log($"[LoginScene] CharacterListNtf received. count={charList.Characters.Count}");

                m_uiLogin.SetStatus($"로그인 성공 (캐릭터 {charList.Characters.Count}개)");

                // 8. CharacterSelection 씬으로 무조건 이동.
                // 캐릭터 선택 / 자동 생성 / 서버 선택 요청 은 그 씬에서 이어짐.
                Managers.Managers.Scene.LoadScene(SceneManagerEx.SceneNames.CharacterSelection);
            }
            catch (Exception e)
            {
                Debug.LogException(e);
                fail($"예외 발생: {e.Message}");
            }
            finally
            {
                m_isLoggingIn = false;
                // 성공해서 씬 전환했다면 이 GameObject는 곧 파괴됨. 그 외 경우 버튼 다시 활성화.
                if (m_uiLogin != null)
                {
                    m_uiLogin.SetLoginButtonInteractable(true);
                }
            }
        }

        // ─── 비동기 헬퍼 ────────────────────────────────────────────────

        // Connect 호출 후 OnConnected 또는 OnDisconnected 이벤트가 올 때까지 대기.
        // 성공이면 true.
        private async Task<bool> connectAsync(string ip, int port)
        {
            m_tcsConnected = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);

            bool started = m_net.Connect(ip, port);
            if (!started)
            {
                return false;
            }

            return await awaitWithTimeout(m_tcsConnected.Task, m_connectTimeoutSec, defaultValue: false);
        }

        // LoginReq를 보내고 LoginRes 수신까지 대기.
        private async Task<LoginRes> sendAndAwaitLoginAsync(string id, string pw)
        {
            m_tcsLoginRes = newTcs<LoginRes>();

            LoginReq req = new LoginReq
            {
                LoginId = id,
                Password = pw
            };
            m_net.Send(GamePacketId.LoginReq, req);

            return await awaitWithTimeout(m_tcsLoginRes.Task, m_packetTimeoutSec);
        }

        // 타임아웃 또는 disconnect 시 null 반환 (T가 참조 타입일 때).
        private async Task<T> awaitWithTimeout<T>(Task<T> task, float timeoutSec) where T : class
        {
            Task delay = Task.Delay(TimeSpan.FromSeconds(timeoutSec));
            Task completed = await Task.WhenAny(task, delay);
            if (completed == task)
            {
                return task.Result;
            }
            return null;
        }

        // bool용 오버로드 (값 타입은 위 메서드 시그니처와 충돌)
        private async Task<bool> awaitWithTimeout(Task<bool> task, float timeoutSec, bool defaultValue)
        {
            Task delay = Task.Delay(TimeSpan.FromSeconds(timeoutSec));
            Task completed = await Task.WhenAny(task, delay);
            if (completed == task)
            {
                return task.Result;
            }
            return defaultValue;
        }

        private TaskCompletionSource<T> newTcs<T>()
        {
            return new TaskCompletionSource<T>(TaskCreationOptions.RunContinuationsAsynchronously);
        }

        // ─── 네트워크 이벤트 ────────────────────────────────────────────

        private void onConnected()
        {
            m_tcsConnected?.TrySetResult(true);
        }

        private void onDisconnected(string reason)
        {
            m_tcsConnected?.TrySetResult(false);
            m_tcsLoginRes?.TrySetResult(null);
            m_tcsGameEnter?.TrySetResult(null);
            m_tcsCharacterList?.TrySetResult(null);

            if (reason != null)
            {
                Debug.LogWarning($"[LoginScene] Disconnected: {reason}");
            }
        }

        // ─── 패킷 핸들러 ────────────────────────────────────────────────

        private void onLoginRes(LoginRes res)
        {
            m_tcsLoginRes?.TrySetResult(res);
        }

        private void onGameEnterNtf(GameEnterNtf ntf)
        {
            m_tcsGameEnter?.TrySetResult(ntf);
        }

        private void onCharacterListNtf(CharacterListNtf ntf)
        {
            m_tcsCharacterList?.TrySetResult(ntf);
        }

        private void onForceDisconnectNtf(ForceDisconnectNtf ntf)
        {
            Debug.LogWarning($"[LoginScene] ForceDisconnectNtf: code={ntf.ReasonCode}, msg={ntf.Message}");
            if (m_uiLogin != null)
            {
                m_uiLogin.SetStatus($"서버로부터 강제 종료: {ntf.Message}");
            }
        }

        // ─── 헬퍼 ───────────────────────────────────────────────────────

        private void fail(string msg)
        {
            if (m_uiLogin != null) m_uiLogin.SetStatus(msg);
            Debug.LogWarning($"[LoginScene] {msg}");
            if (m_net != null && m_net.IsConnected)
            {
                m_net.Disconnect();
            }
        }
    }
}
