using Client.Network;
using Common;
using GamePacket;
using UnityEngine;

namespace Client.Game
{
    // 로컬 플레이어가 prop 근처에서 상호작용 키(Interact, 기본 F)를 누르면
    // 범위 안의 가장 가까운 prop(PropObject) 에 대해 ObjectInteractReq 를 서버에 보낸다.
    // 서버가 권위 위치를 검증한 뒤 상태전이(PropStateNtf) + Lua OnObjectInteract(효과)를 발동한다.
    //
    // prop 은 서버 권위 엔티티(PropObject)다. 씬 배치 마커(PropMarker)는 export 전용이고,
    // 런타임 상호작용 대상은 서버가 prop_spawns 로 스폰한 PropObject(StageManager.m_props)에서 고른다.
    // 플레이어엔 물리 콜라이더가 없어(NavMesh 이동) 평면 거리판정을 쓴다. StageManager 가 런타임 부착.
    public class PropInteractor : MonoBehaviour
    {
        // 클라측 상호작용 대상 선택 반경(평면 X-Z). 실제 사거리 검증은 서버 권위(prop 별 InteractRange).
        // 클라는 "어느 prop 을 가리킬지"만 정하므로 넉넉한 고정값을 쓴다.
        private const float k_selectRange = 3f;

        private bool m_subscribed;

        private void Update()
        {
            // Interact 이벤트 지연 구독(Managers.Input 준비 후 1회).
            if (!m_subscribed)
            {
                Client.Managers.InputManager input = Client.Managers.Managers.Input;
                if (input != null)
                {
                    input.OnInteract += onInteract;
                    m_subscribed = true;
                }
            }
        }

        private void OnDisable()
        {
            if (!m_subscribed)
                return;
            Client.Managers.InputManager input = Client.Managers.Managers.Input;
            if (input != null)
                input.OnInteract -= onInteract;
            m_subscribed = false;
        }

        private void onInteract()
        {
            StageManager sm = StageManager.Instance;
            if (sm == null || sm.IsStageLoading || sm.LocalPlayer == null)
                return;

            Vector3 pos = sm.LocalPlayer.transform.position;

            // 범위 안의 가장 가까운 prop 엔티티 선택(평면 X-Z 거리).
            PropObject best = sm.FindNearestProp(pos, k_selectRange);
            if (best == null)
                return;

            NetworkManager net = NetworkManager.Instance;
            if (net == null || !net.IsConnected)
                return;

            net.Send(GamePacketId.ObjectInteractReq, new ObjectInteractReq
            {
                ObjectId = best.ObjectId,
                PosX = pos.x,
                PosY = pos.y,
                PosZ = pos.z,
            });
        }
    }
}
