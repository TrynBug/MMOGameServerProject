#pragma once

// 참고: PacketGenerator 프로젝트는 PCH를 "사용 안함"으로 설정되어 있습니다. 왜냐하면 protoc로 생성되는 .pb.cc 파일들은 pch를 사용하지 않기 때문입니다.
// PCH가 필요한 cpp 파일에서는 pch.h를 수동으로 포함시켜 주세요.

// Windows
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>

// STL
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

// Protobuf
#include <google/protobuf/message.h>
#include <google/protobuf/json/json.h>

