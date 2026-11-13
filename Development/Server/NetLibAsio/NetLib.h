#pragma once

#pragma comment(lib, "NetLibAsio.lib")
#pragma detect_mismatch("NetLibBackend", "NetLibAsio")

#include "../NetLibCommon/Types.h"
#include "../NetLibCommon/NetDefine.h"
#include "../NetLibCommon/NetConfig.h"
#include "../NetLibCommon/PacketHeader.h"
#include "../NetLibCommon/Packet.h"
#include "../NetLibCommon/PacketPool.h"
#include "../NetLibCommon/ISession.h"
#include "../NetLibCommon/INetEventHandler.h"
#include "../NetLibCommon/FuncEventHandler.h"
#include "../NetLibCommon/Crypto.h"
#include "../NetLibCommon/NetLibStats.h"
#include "IoContext.h"
#include "NetServer.h"
#include "NetClient.h"
