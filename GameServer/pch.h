#pragma once

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>

// STL
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// ServerBase (NetLib 포함)
#include "ServerBaseLib.h"

// DBConnector
#include "DBConnectorLib.h"

// PacketGenerator
#include "ProtoSerializer.h"
#include "ProtoJsonSerializer.h"
#include "Generated/Common/packet_id.pb.h"
#include "Generated/DataStructures/character.pb.h"
#include "Generated/GamePacket/session_packet.pb.h"
#include "Generated/ServerPacket/gateway_user_packet.pb.h"
#include "Generated/ServerPacket/gateway_game_packet.pb.h"
