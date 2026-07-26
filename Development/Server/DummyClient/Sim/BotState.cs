namespace DummyClient.Sim
{
    public enum BotState
    {
        Idle,              // 시작 전 / 재접속 대기 후 진입
        ConnectingLogin,   // 로그인서버 TCP 연결 중
        WaitLoginRes,      // LoginReq 보내고 LoginRes 대기
        NeedGatewayConnect,// 로그인 끊고 게이트웨이 연결 준비
        ConnectingGateway, // 게이트웨이 TCP 연결 중
        WaitCharList,      // GatewayAuthReq 보내고 CharacterListNtf 대기
        WaitCreate,        // CharacterCreateReq 보내고 CharacterCreateRes 대기
        WaitSelect,        // CharacterSelectReq 보내고 CharacterSelectRes 대기
        WaitStageLoad,     // StageLoadCompleteReq 보내고 StageLoadCompleteRes 대기
        InStage,           // 플레이 중 (NavMesh 이동)
        WaitStageMoveRes,  // 포탈 상호작용 후 StageMoveRes 대기 (스테이지 이동 중)
        Disconnected,      // 끊김 (reconnect 설정 시 재접속 대기)
    }
}
