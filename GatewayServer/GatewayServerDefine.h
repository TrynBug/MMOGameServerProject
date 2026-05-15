#pragma once

// 세션 타입
enum class ESessionType : uint8 
{ 
    Unknown,     // 내부 포트로 accept 되었고, 아직 어떤서버인지는 확인안됨
    Client,      // 클라이언트 포트로 accept된 세션
    GameServer,  // 내부 포트, 게임서버 핸드셰이크 완료
    LoginServer  // 내부 포트, 로그인서버 핸드셰이크 완료
};

// 세션 추가 정보
struct SessionMetaInfo
{
    ESessionType sessionType = ESessionType::Unknown;
    int64        userId = 0;         // sessionType==Client 이고 인증 완료된 경우
    int32        gameServerId = 0;   // sessionType==GameServer 인 경우
};

// 이전 접속 게임서버 정보 (5분 TTL)
struct PrevGameServerEntry
{
    int32 gameServerId = 0;
    std::chrono::steady_clock::time_point expireTime;
};

// 인증토큰 관리
struct AuthTokenEntry
{
    uint64 authToken = 0;
    int64 expireTimeMs = 0;   // Unix timestamp ms
};