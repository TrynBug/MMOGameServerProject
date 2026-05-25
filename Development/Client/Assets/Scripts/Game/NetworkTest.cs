using Client.Network;
using UnityEngine;

namespace Client.Game
{
    // Phase 1 검증용 임시 컴포넌트.
    // 빈 씬에 빈 GameObject를 만들고 이 컴포넌트를 부착한 뒤 Play.
    // 콘솔에서 "OnConnected" 로그가 뜨면 네트워크 레이어가 정상 동작하는 것.
    //
    // 검증이 끝나면 이 파일은 삭제하거나 LoginScene 정식 코드로 교체한다.
    public class NetworkTest : MonoBehaviour
    {
        [SerializeField] private string m_ip = "127.0.0.1";
        [SerializeField] private int m_port = 8001;
        [SerializeField] private bool m_connectOnStart = true;

        private NetworkManager m_net;

        private void Start()
        {
            // NetworkManager 는 GameBootstrap 이 이미 생성해둔 상태.
            m_net = NetworkManager.Instance;
            if (m_net == null)
            {
                Debug.LogError("[NetworkTest] NetworkManager.Instance 가 없습니다. GameBootstrap 이 실행되었는지 확인하세요.");
                return;
            }

            m_net.OnConnected += onConnected;
            m_net.OnDisconnected += onDisconnected;

            if (m_connectOnStart)
            {
                Debug.Log($"[NetworkTest] Connecting to {m_ip}:{m_port} ...");
                m_net.Connect(m_ip, m_port);
            }
        }

        private void OnDestroy()
        {
            if (m_net != null)
            {
                m_net.OnConnected -= onConnected;
                m_net.OnDisconnected -= onDisconnected;
            }
        }

        private void onConnected()
        {
            Debug.Log("[NetworkTest] OnConnected");
        }

        private void onDisconnected(string reason)
        {
            if (reason == null)
                Debug.Log("[NetworkTest] OnDisconnected (정상 종료)");
            else
                Debug.LogWarning($"[NetworkTest] OnDisconnected: {reason}");
        }
    }
}
