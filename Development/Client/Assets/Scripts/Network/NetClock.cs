using UnityEngine;

namespace Client.Network
{
    // 원격 액터 보간용 "재생 시계(playback clock)".
    //
    // 서버는 SnapshotNtf 에 단조 증가 server_tick_seq 를 실어 보낸다(서버시각 ≈ seq * 50ms).
    // 클라는 이 시각에서 InterpDelayMs 만큼 과거를 재생(RenderTimeMs)하여, 보간 버퍼에 점이 쌓인 구간을
    // 안전하게 보간한다.
    //
    // 핵심: 재생 시각은 매 프레임 로컬 deltaTime 으로 전진하고, "추정 현재 서버시각"(= 마지막 스냅샷
    // 서버시각 + 그 이후 로컬 경과시간)을 목표로 부드럽게 추종한다. 목표가 스냅샷 공백 동안에도
    // 로컬 시간으로 같이 전진하므로, 스냅샷이 드물게/불규칙하게 와도(또는 한동안 안 와도) 재생 시각이
    // 굶거나 리싱크 점프하지 않는다. → 빈 스냅샷 keepalive 가 필요 없다.
    public static class NetClock
    {
        // 서버 tick 간격(ms). 서버 Stage tick(k_updateTickUnitMs) 과 반드시 일치해야 한다.
        public const double ServerTickIntervalMs = 50.0;

        // 보간 지연(ms). 이만큼 과거를 그린다. 움직이는 객체는 20Hz 로 오므로 2 tick(100ms)면 충분.
        public const double InterpDelayMs = 100.0;

        // 추정 목표와 차이가 이보다 크면(스테이지 이동/장시간 정지 후 등) 부드럽게가 아니라 즉시 정렬.
        private const double k_resyncThresholdMs = 500.0;

        private static bool   s_initialized;
        private static double s_latestServerMs;    // 마지막 수신 스냅샷의 서버시각
        private static double s_localAtLatestMs;   // 그 스냅샷을 받은 로컬시각(ms)
        private static double s_playbackMs;        // 현재 재생 시각 (RenderTimeMs)

        public static double RenderTimeMs => s_playbackMs;

        // 추정 "현재" 서버시각 (보간지연 0). 경로 복제 재생용 — 경로 이벤트의 anchor(start_tick)가
        // emit 시점=현재 tick 이므로, 같은 "현재" 타임라인으로 재생해야 elapsed = ServerNow − anchor ≥ 0.
        // RenderTimeMs(과거 −100ms 보간)와 달리 InterpDelay 만큼 미래다. 동일 스무딩을 공유한다.
        public static double ServerNowMs => s_playbackMs + InterpDelayMs;

        public static bool   IsReady      => s_initialized;

        private static double NowMs => (double)Time.unscaledTime * 1000.0;

        // SnapshotNtf 수신 시 호출. 최신 서버시각과 수신 로컬시각을 갱신한다.
        public static void OnServerTick(uint serverTickSeq)
        {
            double t = serverTickSeq * ServerTickIntervalMs;

            if (!s_initialized)
            {
                s_latestServerMs  = t;
                s_localAtLatestMs = NowMs;
                s_playbackMs      = t - InterpDelayMs;
                s_initialized     = true;
                return;
            }

            // 더 최신 서버시각일 때만 앵커 갱신(순서 어긋난/중복 패킷 무시).
            if (t > s_latestServerMs)
            {
                s_latestServerMs  = t;
                s_localAtLatestMs = NowMs;
            }
        }

        // 매 프레임 호출(StageManager.Update). 재생 시각을 추정 현재 서버시각 - 보간지연으로 추종.
        public static void Tick(float deltaTimeSec)
        {
            if (!s_initialized)
                return;

            s_playbackMs += deltaTimeSec * 1000.0;   // 로컬 시간으로 매 프레임 전진(프레임 부드러움).

            // 추정 현재 서버시각 = 마지막 스냅샷 서버시각 + 그 이후 로컬 경과시간.
            // (스냅샷 공백에도 목표가 계속 전진 → 굶거나 리싱크 점프하지 않음.)
            double estServerNow = s_latestServerMs + (NowMs - s_localAtLatestMs);
            double target = estServerNow - InterpDelayMs;

            double diff = target - s_playbackMs;
            if (diff > k_resyncThresholdMs || diff < -k_resyncThresholdMs)
                s_playbackMs = target;       // 큰 차이는 즉시 정렬(스테이지 이동 등).
            else
                s_playbackMs += diff * 0.1;  // 작은 드리프트는 부드럽게 보정.
        }

        // 스테이지 이동/재접속 등으로 시계를 초기화해야 할 때 호출.
        public static void Reset()
        {
            s_initialized     = false;
            s_latestServerMs  = 0.0;
            s_localAtLatestMs = 0.0;
            s_playbackMs      = 0.0;
        }
    }
}
