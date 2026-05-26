using Client.Managers;
using UnityEngine;

namespace Client.Game
{
    // Login 씬의 BaseScene 구현.
    //
    // Login 씬에 빈 GameObject "@Scene" 을 만들고 이 컴포넌트를 부착하면,
    // Start 시점에 Init() 이 호출된다.
    //
    // 실제 로그인 UI 와 시퀀스는 LoginSceneController 가 담당하므로,
    // 이 클래스는 씬 진입/이탈 시점의 공통 작업만 담당한다.
    public class LoginScene : BaseScene
    {
        protected override void Init()
        {
            Debug.Log("[LoginScene] Init");
            // 추후: 로그인 BGM 재생, 마우스 커서 기본 모양 등이 들어갈 자리
        }

        public override void Clear()
        {
            Debug.Log("[LoginScene] Clear");
            // 추후: BGM 정지, 임시 리소스 해제 등
        }
    }
}
