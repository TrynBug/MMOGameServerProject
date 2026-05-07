#pragma once

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")

// STL
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// NetLib
#include "NetLib.h"

// Protobuf
#include <google/protobuf/message.h>
#include <google/protobuf/json/json.h>

#include "ProtoSerializer.h"
#include "Generated/Common/packet_id.pb.h"
#include "Generated/ServerPacket/server_registry_packet.pb.h"

// Logger
#include "LoggerLib.h"

// ServerBase
#include "Types.h"
#include "Utils.h"
