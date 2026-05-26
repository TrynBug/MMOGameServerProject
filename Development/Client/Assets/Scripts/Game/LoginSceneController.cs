using System;
using System.Threading.Tasks;
using Client.Managers;
using Client.Network;
using Client.Packet;
using Common;
using GamePacket;
using TMPro;
using UnityEngine;
using UnityEngine.UI;

namespace Client.Game
{
    // 로그인 씬의 UI 컨트롤러 + 접속 시퀀스를 함께 담당한다.
    //
    // 흐름 (모두 성공해야 다음 씬으로 넘어감):
    //   1. 로그인서버 connect
    //   2. LoginReq 송신 → LoginRes 수신
    //   3. 로그인서버 disconnect
    //   4. 게이트웨이서버 connect
    //   5. GatewayAuthReq 송신 (응답 패킷 없음)
    //   6. GameEnterNtf 수신 (게임서버가 SystemStage 배정 완료)
    //   7. CharacterListNtf 수신 → 캐시에 저장 → CharacterSelect 씬 로드
    //
    // 어느 단계든 실패하면 모든 연결을 끊고 Login 버튼을 다시 활성화한다.
    // 부분 재시도는 하지 않는다 (인증토큰 5분 유효시간 때문에 처음부터 다시 하는 게 안전).
    public class LoginSceneController : MonoBehaviour
    {
        [Header("Login Server")]
        [SerializeField] private string m_loginIp = "127.0.0.1";
        [SerializeField] private int m_loginPort = 8001;

        [Header("UI - InputFields (TextMeshPro)")]
        [SerializeField] private TMP_InputField m_idInput;
        [SerializeField] private TMP_InputField m_passwordInput;

        [Header("UI - Buttons")]
        [SerializeField] private Button m_loginButton;

        [Header("UI - Status")]
        [SerializeField] private TMP_Text m_statusText;

        [Header("Scene Transition")]
        [Tooltip("CharacterSelect 씬이 아직 없다면 끄세요. 로그인 흐름만 검증할 수 있습니다.")]
        [SerializeField] private bool m_loadNextScene = false;
        [SerializeField] private string m_nextSceneName = "CharacterSelect";

        [Header("Timeouts (seconds)")]
        [SerializeField] private float m_connectTimeoutSec = 10f;
        [SerializeField] private float m_packetTimeoutSec = 10f;

        private NetworkManager m_net;

        // 패킷 수신을 await 가능한 형태로 만들기 위한 TaskCompletionSource.
        // 각 단계 진입 시 새로 만들고, 패킷 핸들러에서 SetResult 호출.
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
                setStatus("초기화 실패: NetworkManager 없음");
                if (m_loginButton != null) m_loginButton.interactable = false;
                return;
            }

            // 네트워크 이벤트 구독
            m_net.OnConnected += onConnected;
            m_net.OnDisconnected += onDisconnected;

            // 패킷 핸들러 등록
            // (StageManager도 같은 패킷을 등록하지만, 같은 ID에 등록하면 덮어씌워지므로
            //  Login 씬에서 처리하는 동안에는 여기로 들어온다. 씬 전환 후 StageManager가 다시 살아나서
            //  자기 핸들러를 등록하면 그쪽으로 가게 된다.)
            PacketDispatcher.Instance.Register<LoginRes>(GamePacketId.LoginRes, onLoginRes);
            PacketDispatcher.Instance.Register<GameEnterNtf>(GamePacketId.GameEnterNtf, onGameEnterNtf);
            PacketDispatcher.Instance.Register<CharacterListNtf>(GamePacketId.CharacterListNtf, onCharacterListNtf);
            PacketDispatcher.Instance.Register<ForceDisconnectNtf>(GamePacketId.ForceDisconnectNtf, onForceDisconnectNtf);

            // 버튼 콜백
            if (m_loginButton != null)
            {
                m_loginButton.onClick.AddListener(onLoginButtonClicked);
            }

            setStatus("ID와 비밀번호를 입력하세요");
        }

        private void OnDestroy()
        {
            if (m_net != null)
            {
                m_net.OnConnected -= onConnected;
                m_net.OnDisconnected -= onDisconnected;
            }

            if (m_loginButton != null)
            {
                m_loginButton.onClick.RemoveListener(onLoginButtonClicked);
            }

            // 패킷 핸들러는 여기서 풀지 않는다.
            // Register는 같은 ID에 덮어쓰기 가능하므로 leak이 아니고,
            // StageManager.Awake가 다시 자기 핸들러로 덮어쓴다.
        }

        // ─── UI 이벤트 ──────────────────────────────────────────────────

        private void onLoginButtonClicked()
        {
            if (m_isLoggingIn) return;

            string id = m_idInput != null ? m_idInput.text.Trim() : "";
            string pw = m_passwordInput != null ? m_passwordInput.text : "";

            if (string.IsNullOrEmpty(id))
            {
                setStatus("ID를 입력하세요");
                return;
            }
            if (string.IsNullOrEmpty(pw))
            {
                setStatus("비밀번호를 입력하세요");
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
            setLoginButtonInteractable(false);

            try
            {
                // 1. 로그인서버 connect
                setStatus("로그인서버 접속 중...");
                if (!await connectAsync(m_loginIp, m_loginPort))
                {
                    fail("로그인서버 접속 실패");
                    return;
                }

                // 2. LoginReq 송신 → LoginRes 대기
                setStatus("로그인 중...");
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

                Debug.Log($"[LoginScene] LoginRes OK. userId={loginRes.UserId}, gateway={loginRes.GatewayIp}:{loginRes.GatewayPort}");

                long userId = loginRes.UserId;
                ulong authToken = loginRes.AuthToken;
                string gatewayIp = loginRes.GatewayIp;
                int gatewayPort = loginRes.GatewayPort;

                // 3. 로그인서버 끊기
                m_net.Disconnect();
                // OnDisconnected가 한번 호출되며 정상 종료로 처리된다.
                // 다음 connect 전에 한 프레임 정도 여유를 두는 게 안전.
                await Task.Yield();

                // 4. 게이트웨이서버 connect
                setStatus("게이트웨이 접속 중...");
                if (!await connectAsync(gatewayIp, gatewayPort))
                {
                    fail("게이트웨이 접속 실패");
                    return;
                }

                // 5. GatewayAuthReq 송신 (응답 패킷 없음, 다음에 GameEnterNtf가 옴)
                setStatus("인증 중...");
                m_tcsGameEnter = newTcs<GameEnterNtf>();
                GatewayAuthReq authReq = new GatewayAuthReq
                {
                    UserId = userId,
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

                // 7. CharacterListNtf 수신 대기
                setStatus("캐릭터 정보 로딩 중...");
                m_tcsCharacterList = newTcs<CharacterListNtf>();
                CharacterListNtf charList = await awaitWithTimeout(m_tcsCharacterList.Task, m_packetTimeoutSec);
                if (charList == null)
                {
                    fail("캐릭터 목록 수신 실패");
                    return;
                }

                // 캐시에 저장
                CharacterDataCache.Instance.SetUserId(userId);
                CharacterDataCache.Instance.SetCharacters(charList.Characters);

                Debug.Log($"[LoginScene] CharacterListNtf received. count={charList.Characters.Count}");

                setStatus($"로그인 성공 (캐릭터 {charList.Characters.Count}개)");

                // 8. 다음 씬으로 전환
                if (m_loadNextScene)
                {
                    Managers.Managers.Scene.LoadScene(m_nextSceneName);
                }
                else
                {
                    Debug.Log("[LoginScene] Scene transition skipped (m_loadNextScene = false).");
                }
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
                setLoginButtonInteractable(true);
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
                // NetworkClient.Connect는 동기로 실패할 수도 있는데,
                // 그 경우 OnDisconnected가 이미 호출됐을 것이라 TCS가 false로 세팅됐을 수도 있음.
                // 안전하게 false 리턴.
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
            // RunContinuationsAsynchronously: TCS.SetResult 호출 스레드에서 continuation이 곧바로 실행되는 걸 막음.
            // 우리는 메인 스레드에서만 SetResult 호출하지만, await 이후 코드를 안전하게 계속 메인 스레드에서 돌게 하기 위함.
            return new TaskCompletionSource<T>(TaskCreationOptions.RunContinuationsAsynchronously);
        }

        // ─── 네트워크 이벤트 ────────────────────────────────────────────

        private void onConnected()
        {
            // 현재 진행 중인 connect 시도의 결과로 처리.
            m_tcsConnected?.TrySetResult(true);
        }

        private void onDisconnected(string reason)
        {
            // 연결 대기 중이었으면 실패로 처리
            m_tcsConnected?.TrySetResult(false);

            // 패킷 대기 중이었으면 null로 깨워줌 (이미 결과가 있으면 무시됨)
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
            // 연결은 곧 끊길 것이므로 OnDisconnected가 알아서 TCS를 깨워줌.
            // StatusText에 사유 표시.
            setStatus($"서버로부터 강제 종료: {ntf.Message}");
        }

        // ─── UI 헬퍼 ───────────────────────────────────────────────────

        private void setStatus(string msg)
        {
            if (m_statusText != null) m_statusText.text = msg;
            Debug.Log($"[LoginScene] {msg}");
        }

        private void setLoginButtonInteractable(bool interactable)
        {
            if (m_loginButton != null) m_loginButton.interactable = interactable;
        }

        private void fail(string msg)
        {
            setStatus(msg);
            // 연결이 남아있으면 정리
            if (m_net != null && m_net.IsConnected)
            {
                m_net.Disconnect();
            }
        }
    }
}
