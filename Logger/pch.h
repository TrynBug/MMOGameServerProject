#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>
#include <chrono>
#include <format>

// spdlog를 컴파일된 라이브러리로 사용한다 (헤더 전용 모드 비활성화)
#define SPDLOG_COMPILED_LIB

#include "spdlog/spdlog.h"
#include "spdlog/async.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/daily_file_sink.h"
