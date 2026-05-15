#pragma once

#include "pch.h"

// 게이트웨이 서버에 접속한 유저를 나타낸다.
// 세션(TCP 소켓), 유저ID, 연결된 게임서버ID 등을 관리한다.
struct GatewayUser
{
    int64               userId        = 0;
    int32               gameServerId  = 0;   // 현재 라우팅된 게임서버 ID (0 = 미배정)
    netlib::ISessionPtr spSession;
    std::string         clientIp;

    bool IsValid() const { return spSession != nullptr && userId != 0; }
};

using GatewayUserPtr = std::shared_ptr<GatewayUser>;
using GatewayUserWPtr = std::weak_ptr<GatewayUser>;
