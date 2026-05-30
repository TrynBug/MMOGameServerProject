using Client.Managers;
using UnityEngine;

namespace Client.Game
{
    // Game 씬의 BaseScene 구현.
    //
    // Game 씬에 빈 GameObject "@Scene" 을 만들고 이 컴포넌트를 부착하면,
    // Start 시점에 Init() 이 호출된다.
    //
    // 책임:
    //   - CharacterDataCache 의 SelectedSpawn 정보로 LocalPlayer 스폰을 StageManager 에 요청
    //
    // 책임이 아닌 것:
    //   - NavMesh 로드 (StageManager 가 StageEnterNtf 수신 시 stage_data_key 로 처리)
    //   - 게임 로직 (StageManager 가 함)
    //   - 캐릭터 GameObject 생성 (CharacterFactory 가 함)
    //   - 패킷 핸들링 (StageManager 가 함)
    public class GameScene : BaseScene
    {
        protected override void Init()
        {
            Debug.Log("[GameScene] Init");

            // 캐시에서 spawn 정보 꺼내기. 정상 흐름이면 CharacterSelector 가 채워놨음.
            SelectedSpawnInfo spawn = CharacterDataCache.Instance.SelectedSpawn;
            if (spawn == null)
            {
                Debug.LogError("[GameScene] SelectedSpawn is null. CharacterSelection 씬을 거치지 않고 진입한 것 같습니다.");
                return;
            }

            // 같은 씬의 StageManager 에 LocalPlayer 스폰 요청.
            // NavMesh 는 StageManager 가 StageEnterNtf 를 받을 때 로드한다 (입장 직후 도착).
            if (StageManager.Instance == null)
            {
                Debug.LogError("[GameScene] StageManager.Instance is null. Game 씬에 StageManager 를 배치했는지 확인하세요.");
                return;
            }

            StageManager.Instance.SpawnLocalPlayer(
                userId: spawn.CharacterId,
                name: spawn.Name,
                pos: spawn.Position,
                dirY: spawn.Yaw);

            Debug.Log($"[GameScene] LocalPlayer spawned. characterId={spawn.CharacterId} stageId={spawn.StageId} pos={spawn.Position} yaw={spawn.Yaw}");
        }

        public override void Clear()
        {
            Debug.Log("[GameScene] Clear");
            // 추후: BGM 정지, HUD 닫기, 캐릭터 정리 등
        }
    }
}
