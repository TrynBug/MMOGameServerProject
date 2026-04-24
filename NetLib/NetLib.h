#pragma once

// NetLib 사용자는 "NetLib.h"를 include 해서 사용한다.
// namespace는 netlib 이다.

#pragma comment(lib, "NetLib.lib")

// 전역 데이터타입
#include "Types.h"

// NetLib public API
#include "NetConfig.h"
#include "PacketHeader.h"
#include "Packet.h"
#include "PacketPool.h"
#include "ISession.h"
#include "INetEventHandler.h"
#include "IoContext.h"
#include "NetServer.h"
#include "NetClient.h"
#include "Crypto.h"
#include "NetLibStats.h"
