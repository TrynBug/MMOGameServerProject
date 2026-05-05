#pragma once

#include <string>
#include <format>
#include <source_location>

// 로그 레벨
enum class LogLevel : int
{
    Debug   = 0,
    Info    = 1,
    Warn    = 2,
    Error   = 3,
    Off     = 4,
};

// spdlog 라이브러리를 사용하는 로거 입니다.
// 로그 전용 스레드 1개가 로그를 처리합니다. 사용자는 LOG_XXX 매크로 호출 시 로그 큐에 삽입만 하고 즉시 반환됩니다.
// 로그파일은 날짜별로 분리되어 기록됩니다.
class Logger
{
public:
    // 초기화
    // @logDir: 로그파일 경로(없으면 자동 생성)
    // @prefix: 로그파일 접두사. 예: "GameServer"
    // @level: 이 레벨 이상의 로그만 출력
    // @asyncQueueSize : 로그큐 크기
    static void Initialize(const std::string& logDir,
                           const std::string& prefix,
                           LogLevel level = LogLevel::Debug,
                           size_t asyncQueueSize = 8192);

    // 큐에 남은 로그를 모두 flush하고 종료
    static void Shutdown();

    // 로그 write
    static void Debug(const std::string& msg, const std::source_location loc = std::source_location::current());
    static void Info (const std::string& msg, const std::source_location loc = std::source_location::current());
    static void Warn (const std::string& msg, const std::source_location loc = std::source_location::current());
    static void Error(const std::string& msg, const std::source_location loc = std::source_location::current());

    // 로그레벨 변경
    static void SetLevel(LogLevel level);

    static bool IsInitialized() { return sm_bInitialized; }

private:
    static inline bool sm_bInitialized = false;
	static inline LogLevel sm_logLevel = LogLevel::Debug;
};


// 로그기록 매크로
#define LOG_DEBUG(msg) Logger::Debug(msg)
#define LOG_INFO(msg)  Logger::Info(msg)
#define LOG_WARN(msg)  Logger::Warn(msg)
#define LOG_ERROR(msg) Logger::Error(msg)
