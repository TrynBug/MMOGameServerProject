#pragma once

// GameServer 전용 상수, enum, 세션 메타정보 구조체 등 정의

// 컨텐츠 스레드 인덱스
// 0번 스레드는 오픈필드 전용, 1번부터는 일반 컨텐츠(공용던전/유저던전 등)
constexpr int32 k_openFieldThreadIndex = 0;

// 오픈필드 Stage ID (고정)
constexpr int64 k_openFieldStageId = 1;


// 게이트웨이서버 세션 추가 데이터
// GameServer가 게이트웨이서버에 connect 한 NetClient의 세션에 부착된다.
// onConnect에서 빈 메타를 부착하고, 핸드셰이크 송신 후 gatewayServerId를 채운다.
struct GatewaySessionMetaInfo
{
    int32 gatewayServerId = 0;
};
