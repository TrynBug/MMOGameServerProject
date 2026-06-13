using UnityEngine;

namespace Client.Game
{
    // 상호작용 오브젝트(문/레버/NPC 등) 배치 마커.
    //
    // 스테이지 씬/프리팹의 prop GameObject 에 붙인다. 중심 = transform.position.
    // 겸용: (1) export 가 읽어 서버 레이아웃 json 의 `markers` 로 출력, (2) 런타임에 PropInteractor 가 근접 판정에 사용.
    // (NavMeshSource·다른 배치 마커와 같은 "씬 오토링 마커" 부류라 NavMesh 어셈블리에 둔다.)
    //
    // 상호작용은 fire-and-forget — 서버가 위치 검증 후 Lua OnObjectInteract(Key, userId) 를 발동한다(상태/동기화 없음).
    public class PropMarker : MonoBehaviour
    {
        public int   Key;
        public int   Type          = 0;    // 용도 구분(문/레버/NPC…). 스크립트가 해석. v1 참고값.
        public float InteractRange  = 2f;   // 상호작용 가능 반경(m, 평면 X-Z).

        private void OnDrawGizmos()
        {
            Gizmos.color = new Color(1f, 0.9f, 0.2f, 0.8f);
            Gizmos.DrawWireSphere(transform.position, InteractRange);
        }
    }
}
