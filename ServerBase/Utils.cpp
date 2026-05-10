#include "pch.h"
#include "Utils.h"


namespace serverbase
{
	LogLevel NetLogLevelToLogLevel(netlib::LogLevel level)
	{
		switch (level)
		{
		case netlib::LogLevel::Debug: return LogLevel::Debug;
		case netlib::LogLevel::Info:  return LogLevel::Info;
		case netlib::LogLevel::Warn:  return LogLevel::Warn;
		case netlib::LogLevel::Error: return LogLevel::Error;
		default:                    return LogLevel::Debug;
		}
	}
} // namespace serverbase
