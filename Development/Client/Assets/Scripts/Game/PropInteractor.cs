using System;
using Client.Network;
using Common;
using GamePacket;
using UnityEngine;

namespace Client.Game
{
    // 로컬 플레이어가 prop(PropMarker) 근처에서 상호작용 키(Interact, 기본 F)를 누르면
    // 범위 안의 가장 가까운 PropMarker 에 대해 ObjectInteractReq 를 서버에 보낸다.
    // 서버가 권위 위치를 검증한 뒤 Lua OnObjectInteract(markerKey, objectId) 를 발동한다(fire-and-forget).
    //
    // 플레이어엔 물리 콜라이더가 없어(NavMesh 이동) 평면 거리판정을 쓴다. StageManager 가 런타임 부착.
    public class PropInteractor : MonoBehaviour
    {
        private PropMarker[] m_markers = Array.Empty<PropMarker>();
        private long m_scannedStageId = 0;
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

            // 스테이지가 바뀌면 prop 마커를 다시 수집(이전 스테이지 마커 무효).
            StageManager sm = StageManager.Instance;
            if (sm != null && sm.CurrentStageId != m_scannedStageId)
            {
                m_markers = UnityEngine.Object.FindObjectsByType<PropMarker>(FindObjectsInactive.Exclude);
                m_scannedStageId = sm.CurrentStageId;
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
            if (sm == null || sm.IsStageLoading || sm.LocalPlayer == null || m_markers.Length == 0)
                return;

            Vector3 pos = sm.LocalPlayer.transform.position;

            // 범위 안의 가장 가까운 prop 선택(평면 X-Z 거리).
            PropMarker best = null;
            float bestSq = float.MaxValue;
            foreach (PropMarker mk in m_markers)
            {
                if (mk == null)
                    continue;
                Vector3 c = mk.transform.position;
                float dx = pos.x - c.x;
                float dz = pos.z - c.z;
                float dsq = dx * dx + dz * dz;
                if (dsq <= mk.InteractRange * mk.InteractRange && dsq < bestSq)
                {
                    bestSq = dsq;
                    best = mk;
                }
            }
            if (best == null)
                return;

            NetworkManager net = NetworkManager.Instance;
            if (net == null || !net.IsConnected)
                return;

            net.Send(GamePacketId.ObjectInteractReq, new ObjectInteractReq
            {
                PropKey = best.Key,
                PosX = pos.x,
                PosY = pos.y,
                PosZ = pos.z,
            });
        }
    }
}
