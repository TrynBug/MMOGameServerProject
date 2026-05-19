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
            // NetworkManager가 씬에 없으면 직접 GameObject 만들어서 부착
            m_net = NetworkManager.Instance;
            if (m_net == null)
            {
                GameObject go = new GameObject("NetworkManager");
                m_net = go.AddComponent<NetworkManager>();
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
