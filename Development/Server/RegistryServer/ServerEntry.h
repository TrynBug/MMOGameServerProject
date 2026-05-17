#pragma once

#include "pch.h"

// 레지스트리 서버가 관리하는 서버 1개의 전체 정보
// ServerInfo(ServerBase/Types.h)는 외부에 전달하는 읽기용 서버정보이고, ServerEntry는 내부에서 세션,하트비트 상태 등을 포함하여 관리하는 데이터이다.
struct ServerEntry
{
    // 식별
    int32        serverId   = 0;
    ServerType   serverType = ServerType::Unknown;
    ServerStatus status     = ServerStatus::Unknown;
    std::string  ip;
    uint16       clientPort = 0;   // 클라 통신용 포트
    uint16       internalPort = 0; // 서버간 통신용 포트
    int32        userCount  = 0;

    // 연결된 세션 (서버가 끊기면 nullptr)
    netlib::ISessionPtr spSession;

    // 하트비트
    std::chrono::steady_clock::time_point lastHeartbeatTime;   // 마지막으로 응답받은 시각

    // ServerInfo 스냅샷 생성 (브로드캐스트용)
    ServerInfo ToServerInfo() const
    {
        ServerInfo info;
        info.serverId   = serverId;
        info.serverType = serverType;
        info.status     = status;
        info.ip         = ip;
        info.clientPort = clientPort;
        info.internalPort = internalPort;
        info.userCount  = userCount;
        return info;
    }
};
