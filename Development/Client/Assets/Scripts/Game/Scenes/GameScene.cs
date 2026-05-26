using Client.Managers;
using UnityEngine;

namespace Client.Game
{
    // Game 씬의 BaseScene 구현.
    //
    // Game 씬에 빈 GameObject "@Scene" 을 만들고 이 컴포넌트를 부착하면,
    // Awake 시점에 Init() 이 호출된다.
    //
    // 실제 게임 로직(스테이지, 캐릭터, 이동 등)은 StageManager 가 담당한다.
    // 이 클래스는 씬 진입/이탈 시점의 공통 작업만 담당한다.
    //
    // 참고: 현재 Game.unity 씬은 아직 만들어지지 않았다. 이 클래스는 미리 준비된 상태로,
    // Game.unity 가 생성되면 그 씬의 "@Scene" 오브젝트에 부착하면 된다.
    public class GameScene : BaseScene
    {
        protected override void Init()
        {
            Debug.Log("[GameScene] Init");
            // 추후: 게임 BGM 재생, 게임 UI(HUD) 표시, 카메라 세팅 등
        }

        public override void Clear()
        {
            Debug.Log("[GameScene] Clear");
            // 추후: BGM 정지, HUD 닫기, 캐릭터 정리 등
        }
    }
}
