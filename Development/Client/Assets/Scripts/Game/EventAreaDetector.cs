using System;
using System.Collections.Generic;
using Client.Network;
using Common;
using GamePacket;
using UnityEngine;

namespace Client.Game
{
    // 로컬 플레이어의 이벤트영역 진입/이탈을 감지해 서버에 보고한다(클라 선판정).
    //
    // 플레이어엔 물리 콜라이더/리지드바디가 없어(NavMesh 이동) Unity OnTriggerEnter 가 동작하지 않는다.
    // 그래서 코드베이스의 다른 플레이어-월드 판정처럼 매 프레임 평면 거리검사를 쓴다.
    // 영역 지오메트리는 스테이지 맵에 배치된 EventAreaMarker 들에서 온다.
    // 서버가 권위 위치로 재검증하므로 클라 오판정은 서버가 거른다(거짓 보고 거부).
    //
    // StageManager 가 자기 GameObject 에 1개 부착한다.
    public class EventAreaDetector : MonoBehaviour
    {
        // 영역 안에 있는 동안 Enter 재전송 간격(초). 서버가 첫 보고를 거부해도(지연 스파이크 등)
        // 다음 재전송에서 자가치유된다. 서버는 occupant 로 중복제거하므로 트리거는 1회만 발동.
        private const float ResendIntervalSec = 0.5f;

        private readonly Dictionary<int, float> m_enterSentAt = new Dictionary<int, float>();   // eventKey -> 마지막 Enter 송신 시각
        private EventAreaMarker[] m_markers = Array.Empty<EventAreaMarker>();
        private long m_scannedStageId = 0;

        private void Update()
        {
            StageManager sm = StageManager.Instance;
            if (sm == null || sm.IsStageLoading || sm.LocalPlayer == null)
                return;

            // 스테이지가 바뀌면 마커를 다시 수집하고 상태를 초기화한다(이전 스테이지 상태 무효).
            if (sm.CurrentStageId != m_scannedStageId)
            {
                m_markers = UnityEngine.Object.FindObjectsByType<EventAreaMarker>(FindObjectsInactive.Exclude);
                m_enterSentAt.Clear();
                m_scannedStageId = sm.CurrentStageId;
            }

            if (m_markers.Length == 0)
                return;

            Vector3 pos = sm.LocalPlayer.transform.position;
            float now = Time.time;
            foreach (EventAreaMarker marker in m_markers)
            {
                if (marker == null)
                    continue;

                bool nowInside = marker.Contains(pos);
                bool wasInside = m_enterSentAt.TryGetValue(marker.EventKey, out float lastSent);

                if (nowInside)
                {
                    // 최초 진입이거나, 안에 있는 동안 재전송 간격이 지났으면 Enter 송신.
                    if (!wasInside || now - lastSent >= ResendIntervalSec)
                    {
                        m_enterSentAt[marker.EventKey] = now;
                        send(GamePacketId.EventAreaEnterReq, marker.EventKey, pos);
                    }
                }
                else if (wasInside)
                {
                    m_enterSentAt.Remove(marker.EventKey);
                    send(GamePacketId.EventAreaExitReq, marker.EventKey, pos);
                }
            }
        }

        private static void send(GamePacketId id, int eventKey, Vector3 pos)
        {
            NetworkManager net = NetworkManager.Instance;
            if (net == null || !net.IsConnected)
                return;

            if (id == GamePacketId.EventAreaEnterReq)
                net.Send(id, new EventAreaEnterReq { EventKey = eventKey, PosX = pos.x, PosY = pos.y, PosZ = pos.z });
            else
                net.Send(id, new EventAreaExitReq  { EventKey = eventKey, PosX = pos.x, PosY = pos.y, PosZ = pos.z });
        }
    }
}
