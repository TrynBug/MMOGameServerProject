using System.Collections.Generic;
using UnityEngine;

namespace Client.Game
{
    // 원격 액터(타 캐릭터/몬스터) 위치 보간 버퍼.
    //
    // 서버 SnapshotNtf 로 들어온 (서버시각, 위치, yaw, 이동중) 샘플을 시계열로 쌓아두고,
    // NetClock.RenderTimeMs(= 최신 서버시각 - 보간지연) 시점을 감싸는 두 샘플 사이를 선형 보간한다.
    // navmesh 길찾기를 쓰지 않으므로 클라/서버 위치가 항상 일치하고 지터에 강하다.
    //
    // 서버 스냅샷 위치는 항상 navmesh 위의 유효한 점이므로, 충분히 잦은 샘플(20Hz) 사이의
    // 직선 보간은 사실상 지형 위를 따라간다.
    public class SnapshotInterpolator
    {
        private struct Entry
        {
            public double TimeMs;
            public Vector3 Pos;
            public float Yaw;
            public bool Moving;
        }

        // 시계열 샘플(TimeMs 오름차순). 보통 2~4개만 유지된다(렌더 시점 이전 1개 + 이후 몇 개).
        private readonly List<Entry> m_buffer = new List<Entry>(8);

        // 위치가 이 거리(유닛) 이상 점프하면(텔레포트/블링크 등) 그 사이를 보간하지 않고 새 시작점으로 스냅한다.
        // 시간 기반 clear 는 가변 송신율에서 유휴→재이동 시 첫 점만 남겨 끊김을 유발했다. 유휴 객체는
        // 위치가 변하지 않으므로(시작점=유휴점) 위치 기반으로 바꾸면 유휴→재이동은 매끈하고, 진짜 점프만 스냅된다.
        private const float k_maxJumpSqr = 64f;   // 8u^2

        public bool HasData => m_buffer.Count > 0;

        // 새 스냅샷 샘플 추가. 과거(또는 동일) 시각의 중복/역순 샘플은 버린다.
        public void Push(double serverTimeMs, Vector3 pos, float yaw, bool moving)
        {
            int n = m_buffer.Count;
            if (n > 0)
            {
                Entry last = m_buffer[n - 1];
                if (serverTimeMs <= last.TimeMs)
                    return;   // 순서 어긋남/중복 → 무시.
                // 위치가 크게 점프하면(텔레포트/블링크) 그 사이를 보간하지 않고 새 시작점으로 스냅.
                // 유휴→재이동은 위치 변화가 거의 없어 clear 되지 않는다(매끄럽게 이어짐).
                if ((pos - last.Pos).sqrMagnitude > k_maxJumpSqr)
                    m_buffer.Clear();
            }

            m_buffer.Add(new Entry { TimeMs = serverTimeMs, Pos = pos, Yaw = yaw, Moving = moving });
        }

        // renderMs 시점의 보간 결과. 데이터가 없으면 false.
        //  - renderMs 가 버퍼 앞/뒤를 벗어나면 양 끝값으로 클램프(hold).
        //  - 보간에 더 이상 필요 없는 과거 샘플은 정리한다.
        public bool Sample(double renderMs, out Vector3 pos, out float yaw, out bool moving)
        {
            pos = Vector3.zero;
            yaw = 0f;
            moving = false;

            int n = m_buffer.Count;
            if (n == 0)
                return false;

            // 렌더 시점이 가장 오래된 샘플보다 과거 → 첫 샘플로 클램프.
            if (renderMs <= m_buffer[0].TimeMs)
            {
                pos = m_buffer[0].Pos;
                yaw = m_buffer[0].Yaw;
                moving = m_buffer[0].Moving;
                return true;
            }

            // 렌더 시점이 최신 샘플보다 미래(버퍼 고갈) → 최신 샘플로 클램프(hold).
            if (renderMs >= m_buffer[n - 1].TimeMs)
            {
                Entry last = m_buffer[n - 1];
                pos = last.Pos;
                yaw = last.Yaw;
                moving = last.Moving;
                return true;
            }

            // renderMs 를 감싸는 두 샘플 [i, i+1] 사이 선형 보간.
            for (int i = 0; i < n - 1; i++)
            {
                if (renderMs >= m_buffer[i].TimeMs && renderMs <= m_buffer[i + 1].TimeMs)
                {
                    double span = m_buffer[i + 1].TimeMs - m_buffer[i].TimeMs;
                    float u = (span > 0.0) ? (float)((renderMs - m_buffer[i].TimeMs) / span) : 0f;

                    pos = Vector3.Lerp(m_buffer[i].Pos, m_buffer[i + 1].Pos, u);
                    yaw = Mathf.LerpAngle(m_buffer[i].Yaw, m_buffer[i + 1].Yaw, u);
                    moving = m_buffer[i + 1].Moving;

                    // i 이전 샘플은 더 이상 필요 없음 → 정리(메모리/탐색비용 절약).
                    if (i > 0)
                        m_buffer.RemoveRange(0, i);

                    return true;
                }
            }

            return false;
        }

        public void Clear()
        {
            m_buffer.Clear();
        }
    }
}
