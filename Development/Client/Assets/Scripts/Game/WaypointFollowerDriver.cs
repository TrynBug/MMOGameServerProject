using System.Collections.Generic;
using UnityEngine;
using Client.Network;

namespace Client.Game
{
    // 경로 의도 복제(dead reckoning) 재생 드라이버. IRemoteMotionDriver 구현.
    //
    // 서버가 변화 시점에만 보낸 [start_tick + move_speed + 폴리라인]을 받아, NetClock.ServerNowMs
    // 기준 경과시간만큼 폴리라인 위를 걸어 위치를 시간의 함수로 재현한다. 서버가 안 보내는 동안에도
    // 알고 있는 끝점까지 같은 경로를 계속 걸으므로 프리즈/외삽 문제가 구조적으로 없다.
    //
    // ★타임라인★ (지난 "뚝뚝 끊김" 버그의 핵심): 경로 anchor(start_tick)는 emit=현재 tick 이므로
    // 반드시 ServerNowMs("현재", 보간지연 0)로 재생해야 elapsed = ServerNow − anchor ≥ 0 이 된다.
    // RenderTimeMs(과거 −100ms)로 재생하면 elapsed 가 음수→시작점 hold→이벤트마다 점프(끊김).
    //
    // waypoints[0] = 송신 시점의 권위 위치 → 매 Move 이벤트가 위치 재정렬(키프레임)도 겸한다.
    public class WaypointFollowerDriver : IRemoteMotionDriver
    {
        // 표시 위치와 권위 재정렬점(waypoints[0])이 이 이상 벌어지면 보간 없이 스냅(블링크/리쉬 복귀 등).
        private const float k_hardSnapSqr = 64f;   // 8u^2

        private readonly List<Vector3> m_path = new List<Vector3>(8);
        private double m_startMs;
        private float  m_moveSpeed;
        private bool   m_moving;

        // 마지막으로 계산/적용한 표시 위치/회전 (정지 hold + 끝점 도달 hold 용).
        private Vector3 m_pos;
        private float   m_yaw;
        private bool    m_hasPos;

        // 서버 MonsterMoveEntry 수신: 경로/속도/시작tick 갱신.
        public void OnMonsterMove(uint startTick, float moveSpeed, IList<float> flatWaypoints)
        {
            m_path.Clear();
            for (int i = 0; i + 2 < flatWaypoints.Count; i += 3)
                m_path.Add(new Vector3(flatWaypoints[i], flatWaypoints[i + 1], flatWaypoints[i + 2]));

            if (m_path.Count == 0)
            {
                m_moving = false;
                return;
            }

            m_startMs   = startTick * NetClock.ServerTickIntervalMs;
            m_moveSpeed = moveSpeed;
            m_moving    = true;

            // waypoints[0] = 권위 재정렬점. 표시 위치가 크게 벌어졌으면(텔포/리쉬) 즉시 스냅.
            // 작은 드리프트는 다음 Sample 이 경로 시작점에서 출발하며 자연 흡수(키프레임마다 재정렬).
            if (!m_hasPos || (m_pos - m_path[0]).sqrMagnitude > k_hardSnapSqr)
            {
                m_pos = m_path[0];
                m_hasPos = true;
            }
        }

        // 서버 MonsterStopEntry 수신: 권위 최종 위치/yaw 로 정지.
        public void OnMonsterStop(Vector3 pos, float yaw, bool teleport)
        {
            m_moving = false;
            m_path.Clear();
            m_pos = pos;     // 권위 최종 위치(teleport/일반 모두 최종은 여기). 위치 변화는 작거나(정지) 의도적(텔포).
            m_yaw = yaw;
            m_hasPos = true;
        }

        public bool Sample(out Vector3 pos, out float yaw, out bool moving)
        {
            pos = m_pos;
            yaw = m_yaw;
            moving = m_moving;

            if (!m_moving || !NetClock.IsReady)
                return m_hasPos;   // 정지/미준비: 마지막 위치 hold(있으면), 없으면 false.

            double elapsed = NetClock.ServerNowMs - m_startMs;
            if (elapsed < 0.0)
            {
                // 재생시계가 아직 start_tick 이전(이벤트 직후 짧은 순간): 시작점 hold.
                m_pos = m_path[0];
                m_hasPos = true;
                pos = m_pos;
                return true;
            }

            // 폴리라인 위에서 traveled 만큼 진행한 점/heading 계산.
            float traveled = m_moveSpeed * (float)(elapsed / 1000.0);
            Vector3 p = m_path[m_path.Count - 1];   // 기본값 = 끝점(경로 초과 시 hold).
            float curYaw = m_yaw;
            bool found = false;

            for (int i = 0; i < m_path.Count - 1; i++)
            {
                Vector3 a = m_path[i];
                Vector3 b = m_path[i + 1];
                float segDx = b.x - a.x;
                float segDz = b.z - a.z;
                float segLen = Mathf.Sqrt(segDx * segDx + segDz * segDz);
                if (segLen < 1e-4f)
                    continue;

                curYaw = Mathf.Atan2(segDx, segDz) * Mathf.Rad2Deg;   // Unity 호환(+Z 정면)
                if (traveled <= segLen)
                {
                    p = Vector3.Lerp(a, b, traveled / segLen);        // Y 포함 보간(지형 높이 따라감)
                    found = true;
                    break;
                }
                traveled -= segLen;
            }

            if (!found)
                p = m_path[m_path.Count - 1];   // 끝점 도달: Stop 이벤트 올 때까지 hold.

            m_pos = p;
            m_yaw = curYaw;
            m_hasPos = true;
            pos = m_pos;
            yaw = m_yaw;
            return true;
        }

        public void Clear()
        {
            m_path.Clear();
            m_moving = false;
            m_hasPos = false;
        }
    }
}
