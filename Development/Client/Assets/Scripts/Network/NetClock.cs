namespace Client.Network
{
    // 원격 액터 보간용 "재생 시계(playback clock)".
    //
    // 서버는 SnapshotNtf 에 단조 증가 server_tick_seq 를 실어 보낸다. 서버 tick 간격은 50ms 이므로
    // 서버시각(ms) ≈ server_tick_seq * 50 으로 환산한다. 클라는 이 시각에서 InterpDelayMs 만큼
    // 과거를 재생(RenderTimeMs)하여, 보간 버퍼에 점이 2개 이상 쌓인 구간을 항상 안전하게 보간한다.
    //
    // 절대 시계 동기 대신, 수신한 최신 서버시각을 목표로 로컬 deltaTime 으로 부드럽게 추종한다.
    // (지터/패킷손실은 버퍼와 ease 추종이 흡수. 큰 점프는 즉시 resync.)
    public static class NetClock
    {
        // 서버 tick 간격(ms). 서버 Stage tick(k_updateTickUnitMs) 과 반드시 일치해야 한다.
        public const double ServerTickIntervalMs = 50.0;

        // 보간 지연(ms). 이만큼 과거를 그린다. 2 tick(=100ms) 여유로 보간 점 2개 확보 + 지터 흡수.
        public const double InterpDelayMs = 100.0;

        // 목표와 차이가 이 값을 넘으면(스파이크/스테이지 이동 등) 부드럽게 따라가지 않고 즉시 resync.
        private const double k_resyncThresholdMs = 500.0;

        private static bool   s_initialized;
        private static double s_latestServerMs;   // 수신한 최신 서버시각
        private static double s_playbackMs;        // 현재 재생 시각 (RenderTimeMs)

        // 현재 렌더 기준 시각(ms). 원격 액터는 이 시각으로 보간 샘플링한다.
        public static double RenderTimeMs => s_playbackMs;

        public static bool IsReady => s_initialized;

        // SnapshotNtf 수신 시 호출. 최신 서버시각을 갱신한다.
        public static void OnServerTick(uint serverTickSeq)
        {
            double t = serverTickSeq * ServerTickIntervalMs;
            if (t > s_latestServerMs)
                s_latestServerMs = t;

            if (!s_initialized)
            {
                s_playbackMs = t;   // 첫 수신: 즉시 정렬.
                s_initialized = true;
            }
        }

        // 매 프레임 호출(StageManager.Update). 재생 시각을 목표(최신 서버시각 - 보간지연)로 추종.
        public static void Tick(float deltaTimeSec)
        {
            if (!s_initialized)
                return;

            s_playbackMs += deltaTimeSec * 1000.0;

            double target = s_latestServerMs - InterpDelayMs;
            double diff = target - s_playbackMs;

            if (diff > k_resyncThresholdMs || diff < -k_resyncThresholdMs)
                s_playbackMs = target;      // 큰 차이는 즉시 resync.
            else
                s_playbackMs += diff * 0.1; // 작은 차이는 부드럽게 추종(드리프트 보정).
        }

        // 스테이지 이동/재접속 등으로 시계를 초기화해야 할 때 호출.
        public static void Reset()
        {
            s_initialized = false;
            s_latestServerMs = 0.0;
            s_playbackMs = 0.0;
        }
    }
}
