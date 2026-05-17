#include "pch.h"
#include "Logger.h"

#include "spdlog/spdlog.h"
#include "spdlog/async.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/daily_file_sink.h"

#include <filesystem>

namespace
{
    // spdlog 레벨 변환
    spdlog::level::level_enum toSpdlogLevel(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Debug: return spdlog::level::debug;
        case LogLevel::Info:  return spdlog::level::info;
        case LogLevel::Warn:  return spdlog::level::warn;
        case LogLevel::Error: return spdlog::level::err;
        default:              return spdlog::level::debug;
        }
    }
}

void Logger::Initialize(const std::string& logDir,
                        const std::string& prefix,
                        LogLevel level,
                        size_t asyncQueueSize)
{
    if (sm_bInitialized)
        return;

	// 현재시간을 YYYYMMDD_HHMMSS 형식으로 문자열로 변환 (로그파일명에 사용)
    auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    std::string strNow = std::format("{:%Y%m%d_%H%M%S}", now);

    // 로그 경로 폴더생성
    std::filesystem::create_directories(logDir);

    // global 스레드풀 초기화 (로그 전용 스레드 1개)
    spdlog::init_thread_pool(asyncQueueSize, 1);

    // 콘솔 출력용 sink 생성 (색상 출력 sink)
    auto spConsoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    spConsoleSink->set_level(toSpdlogLevel(level));

    // 파일 출력용 sink 생성 (날짜별로 파일이 생성된다)
    std::string filePath = logDir + "/" + prefix + "_" + strNow;
    auto spFileSink = std::make_shared<spdlog::sinks::daily_file_format_sink_mt>(filePath + "_%Y-%m-%d.log", 0, 0);
    spFileSink->set_level(toSpdlogLevel(level));

    // 비동기 로거 생성
    auto spLogger = std::make_shared<spdlog::async_logger>(
        "server",                                              // 로거 이름(내부 식별용)
        spdlog::sinks_init_list{ spConsoleSink, spFileSink },  // sink 등록
        spdlog::thread_pool(),                                 // global 스레드풀 사용
        spdlog::async_overflow_policy::block                   // 로그큐가 가득 찼을 때의 정책: 호출 스레드를 block한다.
    );

    spLogger->set_level(toSpdlogLevel(level));

    // 로그 포맷: [시각] [레벨] 메시지
    spLogger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

    // 전역 로거로 등록
    spdlog::register_logger(spLogger);
    spdlog::set_default_logger(spLogger);

    // 매 10초마다 flush 하도록 설정
    spdlog::flush_every(std::chrono::seconds(10));

    sm_logLevel = level;
    sm_bInitialized = true;
}

void Logger::Shutdown()
{
    if (!sm_bInitialized)
        return;

    // 큐에 남은 로그를 모두 flush하고 종료
    spdlog::shutdown();
    sm_bInitialized = false;
}

void Logger::LogWrite(const LogLevel logLevel, const std::string& msg, const std::source_location loc)
{
    switch (logLevel)
    {
    case LogLevel::Debug: 
        spdlog::debug(std::format("[{}:{}] {}", loc.function_name(), loc.line(), msg));
        break;
    case LogLevel::Info: 
        spdlog::info(std::format("[{}:{}] {}", loc.function_name(), loc.line(), msg));
        break;
    case LogLevel::Warn: 
        spdlog::warn(std::format("[{}:{}] {}", loc.function_name(), loc.line(), msg));
        break;
    case LogLevel::Error: 
        spdlog::error(std::format("[{}:{}] {}", loc.function_name(), loc.line(), msg));
        break;
    default: 
        spdlog::error(std::format("[{}:{}] {}", loc.function_name(), loc.line(), msg));
        break;
    }
}

void Logger::SetLevel(LogLevel level)
{
    sm_logLevel = level;
    spdlog::set_level(toSpdlogLevel(level));
}


// 로그레벨 -> string
std::string Logger::LogLevelToString(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Debug: return "Debug";
    case LogLevel::Info: return "Info";
    case LogLevel::Warn: return "Warn";
    case LogLevel::Error: return "Error";
    default: return "unknown";
    }
}

// string -> 로그레벨
LogLevel Logger::StringToLogLevel(const std::string& str)
{
    if (str == "Debug") return LogLevel::Debug;
    if (str == "Info")  return LogLevel::Info;
    if (str == "Warn")  return LogLevel::Warn;
    if (str == "Error") return LogLevel::Error;

    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });

    if (lower == "debug") return LogLevel::Debug;
    if (lower == "info")  return LogLevel::Info;
    if (lower == "warn")  return LogLevel::Warn;
    if (lower == "error") return LogLevel::Error;

    return LogLevel::Debug; // 기본값
}