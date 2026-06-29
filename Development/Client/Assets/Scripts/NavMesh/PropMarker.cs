using UnityEngine;

namespace Client.Game
{
    // 상호작용 오브젝트(문/레버/스위치/포탈 등) 배치 마커.
    //
    // 스테이지 씬/프리팹의 prop 배치 지점에 붙인다. 중심 = transform.position, 방향 = transform.eulerAngles.y.
    // 역할: **export 전용** — 서버 레이아웃 json 의 `props` 로 출력된다(key/type/pos/yaw/range/initialState/param).
    // (NavMeshSource·다른 배치 마커와 같은 "씬 오토링 마커" 부류라 NavMesh 어셈블리에 둔다.)
    //
    // 런타임 prop 은 서버가 prop_spawns 로 스폰하는 엔티티(PropObject)다. 근접 상호작용 판정은
    // 스폰된 PropObject 를 대상으로 하므로(PropInteractor) 이 마커는 더 이상 런타임에 쓰이지 않는다.
    // 상태머신/게이팅/선언형동작은 GameData_Prop(Type) 이 정의한다.
    public class PropMarker : MonoBehaviour
    {
        public int   Key;                   // 배치 인스턴스 키(작성자 지정). 스크립트 OnObjectInteract 분기용 안정 식별자.
        public int   Type          = 0;     // GameData_Prop.Key (= prop 종류). 상태머신/사거리/동작의 출처.
        public float InteractRange = 2f;    // 상호작용 반경(m, 평면 X-Z) override. <=0 이면 GameData_Prop.InteractRange 사용.
        public int   InitialState  = -1;    // 시작상태 override. <0 이면 GameData_Prop.InitialState 사용.
        public int   Param0        = 0;     // 배치별 동작 파라미터(예: 포탈 목적지 스테이지 override). 0 = 미사용.
        public int   Param1        = 0;     // 배치별 동작 파라미터(예비).

        private void OnDrawGizmos()
        {
            Gizmos.color = new Color(1f, 0.9f, 0.2f, 0.8f);
            Gizmos.DrawWireSphere(transform.position, InteractRange);
        }
    }
}
