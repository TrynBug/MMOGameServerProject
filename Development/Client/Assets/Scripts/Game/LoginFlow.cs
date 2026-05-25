using Client.Network;
using Client.Packet;
using Common;
using GamePacket;
using UnityEngine;

namespace Client.Game
{
    // 로그인 → 게이트웨이 인증까지의 상태머신.
    // GatewayAuthReq를 보내고 나면 자기 역할 끝. 그 이후의 게임 세계 패킷(GameEnterNtf 등)은
    // StageManager가 처리한다.
    //
    // 빈 GameObject에 부착하고 Inspector에서 LoginServer IP/Port와 ID/PW를 설정 후 Play.
    public class LoginFlow : MonoBehaviour
    {
        private enum State
        {
            Idle,
            ConnectingLogin,
            WaitingLoginRes,
            ConnectingGateway,
            AuthSent,    // GatewayAuthReq 송신 완료. 이 시점부터 StageManager가 책임짐.
            Failed
        }

        [Header("Login Server")]
        [SerializeField] private string m_loginIp = "127.0.0.1";
        [SerializeField] private int m_loginPort = 8001;

        [Header("Account")]
        [SerializeField] private string m_loginId = "test";
        [SerializeField] private string m_password = "test";

        private NetworkManager m_net;
        private State m_state = State.Idle;

        // 로그인 응답에서 받은 정보 (게이트웨이 접속에 사용)
        private long m_userId;
        private ulong m_authToken;
        private string m_gatewayIp;
        private int m_gatewayPort;

        private void Start()
        {
            // NetworkManager 는 GameBootstrap 이 이미 생성하고 PacketDispatcher 와도 연결해둔 상태.
            m_net = NetworkManager.Instance;
            if (m_net == null)
            {
                Debug.LogError("[LoginFlow] NetworkManager.Instance 가 없습니다. GameBootstrap 이 실행되었는지 확인하세요.");
                return;
            }

            // 로그인 관련 핸들러만 등록. GameEnterNtf/StageEnterNtf 는 StageManager가 등록.
            PacketDispatcher.Instance.Register<LoginRes>(GamePacketId.LoginRes, onLoginRes);
            PacketDispatcher.Instance.Register<ForceDisconnectNtf>(GamePacketId.ForceDisconnectNtf, onForceDisconnectNtf);
            PacketDispatcher.Instance.OnUnknownPacket = id => Debug.LogWarning($"[LoginFlow] Unknown packet id: {id}");

            m_net.OnConnected += onConnected;
            m_net.OnDisconnected += onDisconnected;

            startLogin();
        }

        private void OnDestroy()
        {
            if (m_net != null)
            {
                m_net.OnConnected -= onConnected;
                m_net.OnDisconnected -= onDisconnected;
            }
        }

        // ─── 단계 진입 ───────────────────────────────────────────────────

        private void startLogin()
        {
            m_state = State.ConnectingLogin;
            Debug.Log($"[LoginFlow] (1) Connecting to LoginServer {m_loginIp}:{m_loginPort} ...");
            m_net.Connect(m_loginIp, m_loginPort);
        }

        private void sendLoginReq()
        {
            m_state = State.WaitingLoginRes;
            LoginReq req = new LoginReq
            {
                LoginId = m_loginId,
                Password = m_password
            };
            Debug.Log($"[LoginFlow] (2) Sending LoginReq id={m_loginId}");
            m_net.Send(GamePacketId.LoginReq, req);
        }

        private void connectGateway()
        {
            m_state = State.ConnectingGateway;
            Debug.Log($"[LoginFlow] (4) Connecting to GatewayServer {m_gatewayIp}:{m_gatewayPort} ...");
            m_net.Connect(m_gatewayIp, m_gatewayPort);
        }

        private void sendGatewayAuth()
        {
            m_state = State.AuthSent;
            GatewayAuthReq req = new GatewayAuthReq
            {
                UserId = m_userId,
                AuthToken = m_authToken
            };
            Debug.Log($"[LoginFlow] (5) Sending GatewayAuthReq userId={m_userId}");
            m_net.Send(GamePacketId.GatewayAuthReq, req);
            Debug.Log("[LoginFlow] === Auth flow complete. StageManager takes over. ===");
        }

        // ─── 네트워크 이벤트 ────────────────────────────────────────────

        private void onConnected()
        {
            switch (m_state)
            {
                case State.ConnectingLogin:
                    Debug.Log("[LoginFlow] Connected to LoginServer.");
                    sendLoginReq();
                    break;
                case State.ConnectingGateway:
                    Debug.Log("[LoginFlow] Connected to GatewayServer.");
                    sendGatewayAuth();
                    break;
                default:
                    Debug.LogWarning($"[LoginFlow] Unexpected OnConnected in state={m_state}");
                    break;
            }
        }

        private void onDisconnected(string reason)
        {
            if (m_state == State.AuthSent)
            {
                Debug.Log($"[LoginFlow] Disconnected after auth. reason={reason ?? "(normal)"}");
                return;
            }

            if (reason == null)
            {
                Debug.Log($"[LoginFlow] Disconnected (normal) in state={m_state}");
            }
            else
            {
                Debug.LogWarning($"[LoginFlow] Disconnected in state={m_state}: {reason}");
                m_state = State.Failed;
            }
        }

        // ─── 패킷 핸들러 ────────────────────────────────────────────────

        private void onLoginRes(LoginRes res)
        {
            Debug.Log($"[LoginFlow] (3) LoginRes received. success={res.Success}");

            if (!res.Success)
            {
                Debug.LogError($"[LoginFlow] Login failed: {res.ErrorMsg}");
                m_state = State.Failed;
                m_net.Disconnect();
                return;
            }

            m_userId = res.UserId;
            m_authToken = res.AuthToken;
            m_gatewayIp = res.GatewayIp;
            m_gatewayPort = res.GatewayPort;

            Debug.Log($"[LoginFlow] Got userId={m_userId}, token={m_authToken}, gateway={m_gatewayIp}:{m_gatewayPort}");

            // 로그인서버와의 연결을 끊고 게이트웨이로 이동.
            m_net.Disconnect();
            connectGateway();
        }

        private void onForceDisconnectNtf(ForceDisconnectNtf ntf)
        {
            Debug.LogWarning($"[LoginFlow] ForceDisconnectNtf: code={ntf.ReasonCode}, msg={ntf.Message}");
        }
    }
}
