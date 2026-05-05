#pragma once

// Logger 라이브러리 사용자는 이 헤더 하나만 include한다.

#pragma comment(lib, "Logger.lib")

// spdlog를 컴파일된 라이브러리로 사용한다 (헤더 전용 모드 비활성화)
#define SPDLOG_COMPILED_LIB

#include "spdlog/spdlog.h"

#include "Logger.h"
