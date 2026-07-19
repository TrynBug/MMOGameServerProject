#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>

#include <format>
#include <memory>
#include <string>

#include "ServerBaseLib.h"
#include "ThreadSafeUnorderedMap.h"
#include "Generated/Common/packet_id.pb.h"
#include "Generated/ServerPacket/server_handshake_packet.pb.h"
#include "Generated/ServerPacket/chat_server_packet.pb.h"
