using UnityEngine;

namespace Client.Managers
{
    // 모든 씬의 베이스 클래스. 각 씬에 하나의 BaseScene 상속 컴포넌트를 부착.
    //
    // 역할:
    //   - 씬별 초기화 로직의 진입점 (Init)
    //   - 씬 전환 시 정리 로직의 진입점 (Clear)
    //   - SceneManagerEx.CurrentScene 으로 외부에서 현재 씬 식별 가능
    //
    // 부착 방법:
    //   씬에 빈 GameObject "@Scene" 을 만들고 LoginScene / GameScene 등 컴포넌트 추가.
    //   Awake 에서 자동으로 Init() 호출됨.
    public abstract class BaseScene : MonoBehaviour
    {
        // 자식 클래스에서 override 가능. base.Awake() 호출 필수.
        protected virtual void Awake()
        {
            Init();
        }

        // 씬 진입 시 1회 호출. 씬별 초기 세팅 (BGM 재생, 커서 변경 등).
        protected abstract void Init();

        // 씬 전환 직전에 SceneManagerEx 가 호출. 씬별 정리 로직.
        public abstract void Clear();
    }
}
