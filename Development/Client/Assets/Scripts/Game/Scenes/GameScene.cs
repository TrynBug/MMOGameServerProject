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
    //   - 캐시의 LocalCharacter 데이터모델로 LocalPlayer GameObject 1회 생성(비활성 상태)
    //   - StageManager 에 스테이지 로딩 시작 요청 (BeginStageLoad)
    //     → 로딩 완료 보고(StageLoadCompleteReq) → 서버가 스폰 → StageLoadCompleteRes 로 LocalPlayer 활성화/배치
    //
    // 책임이 아닌 것:
    //   - 게임 로직 (StageManager 가 함)
    //   - 캐릭터 GameObject 생성 자체 (CharacterFactory 가 함)
    //   - 패킷 핸들링 (StageManager 가 함)
    public class GameScene : BaseScene
    {
        protected override void Init()
        {
            Debug.Log("[GameScene] Init");

            // 캐시에서 내 캐릭터 데이터모델 꺼내기. 정상 흐름이면 CharacterSelector 가 채워놨음.
            DataStructures.Character localData = CharacterDataCache.Instance.LocalCharacter;
            if (localData == null)
            {
                Debug.LogError("[GameScene] LocalCharacter is null. CharacterSelection 씬을 거치지 않고 진입한 것 같습니다.");
                return;
            }

            if (StageManager.Instance == null)
            {
                Debug.LogError("[GameScene] StageManager.Instance is null. Game 씬에 StageManager 를 배치했는지 확인하세요.");
                return;
            }

            // 데이터모델로 LocalPlayer 를 1회 생성한다 (비활성/조작불가 상태로 보관).
            // 이후 스테이지를 이동해도 파괴하지 않고 숨김+재배치만 한다.
            StageManager.Instance.EnsureLocalPlayer(localData);

            // 스테이지 로딩 시작 (NavMesh 교체 → StageLoadCompleteReq 송신).
            // LocalPlayer 는 이후 StageLoadCompleteRes 수신 시 활성화+배치된다.
            StageManager.Instance.BeginStageLoad(CharacterDataCache.Instance.SelectedStageDataKey);

            // 스테이지 이동 치트 키 (F5~F8). 코드로 부착하여 씬 에셋 수정 불필요.
            gameObject.AddComponent<StageMoveCheat>();

            // Create the local player's buff bar HUD (code-built, no prefab/art needed).
            Managers.Managers.UI.ShowSceneUI<UI_BuffBar>();
        }

        public override void Clear()
        {
            Debug.Log("[GameScene] Clear");
            // 추후: BGM 정지, HUD 닫기, 캐릭터 정리 등
        }
    }
}
