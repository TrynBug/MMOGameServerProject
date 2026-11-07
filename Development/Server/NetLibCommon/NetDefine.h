#pragma once

#include "Types.h"

#include <memory>

namespace netlib
{

// 로그 레벨
enum class LogLevel : int
{
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
};

}
