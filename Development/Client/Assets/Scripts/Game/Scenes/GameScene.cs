using Client.Managers;
using GameData;
using MMO.Client.Navigation;
using UnityEngine;

namespace Client.Game
{
    // Game 씬의 BaseScene 구현.
    //
    // Game 씬에 빈 GameObject "@Scene" 을 만들고 이 컴포넌트를 부착하면,
    // Start 시점에 Init() 이 호출된다.
    //
    // 책임:
    //   - 씬 진입 시 SelectedSpawn 의 StageId 로 NavMesh 로드
    //   - CharacterDataCache 의 SelectedSpawn 정보로 LocalPlayer 스폰을 StageManager 에 요청
    //
    // 책임이 아닌 것:
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

            // NavMesh 로드. 캐릭터 스폰 전에 먼저 해야 PlayerCharacter 의 길찾기가 즉시 동작함.
            // stageId → NavMeshFileName 매핑은 게임데이터에서.
            loadNavMeshForStage(spawn.StageId);

            // 같은 씬의 StageManager 에 LocalPlayer 스폰 요청.
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

        // stageId 로 게임데이터를 조회해서 NavMesh 파일을 로드.
        // 게임데이터가 없거나 NavMeshFileName 이 비어있으면 경고만 남기고 진행
        // (PlayerCharacter 는 NavMesh 없으면 직선 이동으로 폴백함).
        private static void loadNavMeshForStage(long stageId)
        {
            GameData_Stage stageData = GameDataTable_Stage.FindData(stageId);
            if (stageData == null)
            {
                Debug.LogError($"[GameScene] Stage 게임데이터를 찾을 수 없습니다. stageId={stageId}");
                return;
            }

            string navMeshName = stageData.NavMeshFileName;
            if (string.IsNullOrEmpty(navMeshName))
            {
                Debug.LogWarning($"[GameScene] Stage 의 NavMeshFileName 이 비어있습니다. stageId={stageId} name={stageData.Name}");
                return;
            }

            if (!NavMeshService.Load(navMeshName))
            {
                Debug.LogError($"[GameScene] NavMesh 로드 실패. stageId={stageId} navMeshName={navMeshName}");
            }
        }
    }
}
