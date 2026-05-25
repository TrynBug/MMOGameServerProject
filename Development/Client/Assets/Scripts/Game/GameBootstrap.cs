using Client.Network;
using Client.Packet;
using UnityEngine;

namespace Client.Game
{
    // 게임 시작 시 한 번만 실행되는 부트스트랩 로직.
    //
    // RuntimeInitializeOnLoadMethod 를 사용하므로 어느 씬에서 Play 해도 자동 실행된다.
    // (GameDataManager.LoadAllGameData 와 동일한 방식)
    //
    // 책임:
    //   1) NetworkManager GameObject 생성 (DontDestroyOnLoad 는 NetworkManager.Awake 가 처리)
    //   2) Network 어셈블리는 Packet 을 모르므로, OnPacketReceived 를 PacketDispatcher 에 연결하는 다리 놓기
    //
    // 향후 다른 전역 매니저 (예: AudioManager, InputManager) 가 추가되면 여기에 넣는다.
    public static class GameBootstrap
    {
        // AfterSceneLoad 사용 이유:
        //   - GameDataManager.LoadAllGameData 가 BeforeSceneLoad 에서 게임데이터를 로드한다.
        //   - 게임데이터 로드 실패 시 Application.Quit 하므로, 그 이후에 NetworkManager 가 생성되어야 안전.
        //   - 또한 BeforeSceneLoad 시점에는 씬의 GameObject 가 존재하지 않아 일부 Unity API 가 불안정함.
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
        private static void Bootstrap()
        {
            // NetworkManager 생성 (싱글톤 패턴: Awake 에서 Instance 세팅 + DontDestroyOnLoad)
            GameObject go = new GameObject("[NetworkManager]");
            NetworkManager net = go.AddComponent<NetworkManager>();

            // Network 어셈블리가 Packet 을 모르므로, 수신 이벤트를 PacketDispatcher 에 연결.
            // 의존 방향: Game → Packet → Network 유지.
            net.OnPacketReceived += PacketDispatcher.Instance.Dispatch;

            Debug.Log("[GameBootstrap] Initialized: NetworkManager + PacketDispatcher bound.");
        }
    }
}
