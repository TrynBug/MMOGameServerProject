#pragma once

#include "MetricsRegistry.h"
#include "Types.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace httplib
{
class Response;
class Server;
}

namespace serverbase
{

// 각 서버 프로세스가 Prometheus scrape용으로 열 내부 HTTP endpoint 주소다.
// 외부 공개 API가 아니므로 기본값은 loopback이며, 서버 종류별 INI에서 서로 다른 port를 지정한다.
struct MetricsHttpServerConfig
{
    std::string ip = "127.0.0.1";
    uint16 port = 0;
};

// cpp-httplib를 사용해 /metrics와 /health만 제공하는 작은 내부 HTTP 서버다.
// accept/HTTP worker는 게임 IOCP worker와 분리되어 있으므로 느린 monitoring client가 게임 packet 처리를 점유하지 않는다.
// MetricsRegistry와 beforeCollect callback이 참조하는 ServerBase/subsystem은 이 객체보다 오래 살아 있어야 한다.
// ServerBase는 shutdown 시 HTTP worker를 먼저 join한 뒤 subsystem과 Registry 참조의 수명을 종료한다.
class MetricsHttpServer
{
public:
    MetricsHttpServer();
    ~MetricsHttpServer();

    MetricsHttpServer(const MetricsHttpServer&) = delete;
    MetricsHttpServer& operator=(const MetricsHttpServer&) = delete;

    // metric self-series를 등록하고 port bind까지 성공한 뒤 반환한다.
    // beforeCollect 실행 순서:
    //   GET /metrics -> subsystem 최신 snapshot 게시 -> Registry text 직렬화 -> HTTP response
    // callback은 cpp-httplib monitoring worker에서 실행되므로 game thread 전용 container를 직접 읽지 말고
    // atomic 값, thread-safe size/snapshot, 짧은 Windows API 조회만 수행해야 한다.
    bool Start(const MetricsHttpServerConfig& config, MetricsRegistry& registry, std::function<void()> beforeCollect, std::string& outError);

    // 새 연결을 중단하고 진행 중인 worker를 join한 뒤 registry/callback 참조를 해제한다. 반복 호출해도 안전하다.
    void Stop();

    // 운영 상태 표시용 근사값이다. 시작/종료 동기화 자체는 Start와 Stop으로 수행한다.
    bool IsRunning() const { return m_bRunning.load(std::memory_order_relaxed); }

private:
    void threadProc();
    void handleMetrics(httplib::Response& response);

private:
    // m_pHttpServer는 cpp-httplib의 listener와 worker queue를 소유한다.
    // httplib.h가 다른 서버 프로젝트로 전파되지 않도록 header에서는 Server를 forward declaration한다.
    std::atomic<bool> m_bRunning { false };
    std::unique_ptr<httplib::Server> m_pHttpServer;
    std::thread m_thread;

    // 아래 non-owning 참조는 Start에서 연결되고 Stop에서 worker 종료 후 해제된다.
    // m_beforeCollect는 ServerBase의 publisher들을 HTTP 계층이 직접 알지 않도록 하는 scrape 직전 갱신 hook이다.
    MetricsRegistry* m_pRegistry = nullptr;
    std::function<void()> m_beforeCollect;

    // HTTP self-metric도 Registry enum으로 갱신한다. 별도 pointer와 null 검사는 두지 않는다.
};

} // namespace serverbase
