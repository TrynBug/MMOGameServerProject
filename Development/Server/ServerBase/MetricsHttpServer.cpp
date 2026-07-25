#include "pch.h"
#include "MetricsHttpServer.h"

#include <httplib.h>

namespace serverbase
{

MetricsHttpServer::MetricsHttpServer() = default;

MetricsHttpServer::~MetricsHttpServer()
{
    Stop();
}

bool MetricsHttpServer::Start(const MetricsHttpServerConfig& config, MetricsRegistry& registry, std::function<void()> beforeCollect,
    std::string& outError)
{
    // Start는 ServerBase 초기화 thread에서 한 번 호출된다. endpoint를 열 수 없으면 target이 영구적으로 down으로 보이므로
    // 초기화 실패를 caller에게 반환한다.
    if (m_bRunning.load(std::memory_order_relaxed))
    {
        outError = "metrics HTTP server is already running";
        return false;
    }
    if (config.port == 0)
    {
        outError = "metrics port must be greater than 0";
        return false;
    }

    // self metric도 같은 registry가 소유한다.
    // scrape_total/error_total은 endpoint 정상 여부, duration/response_bytes는 수집 비용과 cardinality 증가를 관찰하기 위한 값이다.
    bool registered = true;
    registered &= registry.AddCounter(CounterMetric::MetricsHttp_ScrapesTotal, "mmo_metrics_scrapes_total", "Total number of successful Prometheus scrapes.");
    registered &= registry.AddCounter(CounterMetric::MetricsHttp_ScrapeErrorsTotal, "mmo_metrics_scrape_errors_total", "Total number of failed Prometheus scrapes.");
    registered &= registry.AddHistogram(HistogramMetric::MetricsHttp_ScrapeDurationSeconds, "mmo_metrics_scrape_duration_seconds", "Time spent collecting and rendering one metrics response.",
        { 0.0001, 0.0005, 0.001, 0.0025, 0.005, 0.01, 0.025, 0.05, 0.1 });
    registered &= registry.AddGauge(GaugeMetric::MetricsHttp_ResponseBytes, "mmo_metrics_response_bytes", "Size of the most recently rendered Prometheus response in bytes.");
    if (!registered)
    {
        outError = "failed to register metrics HTTP server self metrics";
        return false;
    }

    m_pRegistry = &registry;
    // beforeCollect는 container를 직접 읽는 함수가 아니라 각 subsystem의 안전한 snapshot을 metric에 게시하는 callback이다.
    // HTTP 계층은 Net/DB/Windows/GameServer 타입을 몰라도 이 callback 하나로 최신값 게시를 요청할 수 있다.
    m_beforeCollect = std::move(beforeCollect);

    // cpp-httplib 객체는 이 wrapper만 소유한다. TLS/압축 매크로를 켜지 않아 외부 binary 의존성 없이 평문 HTTP만 사용한다.
    auto httpServer = std::make_unique<httplib::Server>();
    // 기존 구현과 동일하게 request header, body, socket 점유 시간을 제한하고 한 번 응답한 연결은 닫는다.
    httpServer->set_read_timeout(1, 0);
    httpServer->set_write_timeout(1, 0);
    httpServer->set_payload_max_length(8192);
    httpServer->set_keep_alive_max_count(1);
    // Prometheus scrape는 짧고 주기적이므로 worker 하나와 작은 대기열이면 충분하다.
    httpServer->new_task_queue = [] { return new httplib::ThreadPool(1, 1, 4); };
    // 다른 process가 동일 monitoring port를 공유하지 못하게 해 잘못된 target을 정상으로 판단하는 상황을 막는다.
    httpServer->set_socket_options([](socket_t socket) {
        httplib::set_socket_opt(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, 1);
    });

    // route handler는 cpp-httplib worker에서 실행된다. /health는 HTTP endpoint 생존만 확인하고,
    // 게임 서비스 준비 여부는 /metrics의 mmo_server_ready 값으로 별도 판단한다.
    httpServer->Get("/metrics", [this](const httplib::Request&, httplib::Response& response) {
        handleMetrics(response);
    });
    httpServer->Get("/health", [](const httplib::Request&, httplib::Response& response) {
        response.set_content("ok\n", "text/plain; charset=utf-8");
    });

    // thread를 시작하기 전에 bind해 잘못된 IP나 이미 사용 중인 port를 Start 호출자에게 즉시 알린다.
    if (!httpServer->bind_to_port(config.ip, config.port))
    {
        outError = std::format("metrics bind failed. ip={} port={}", config.ip, config.port);
        m_beforeCollect = {};
        m_pRegistry = nullptr;
        return false;
    }

    m_pHttpServer = std::move(httpServer);
    m_bRunning.store(true, std::memory_order_release);

    try
    {
        m_thread = std::thread(&MetricsHttpServer::threadProc, this);
    }
    catch (const std::exception& e)
    {
        outError = std::format("failed to start metrics thread: {}", e.what());
        Stop();
        return false;
    }

    // Start가 성공한 뒤 Stop이 호출되면 listener가 반드시 실행 중인 상태가 되도록 동기화한다.
    m_pHttpServer->wait_until_ready();

    return true;
}

void MetricsHttpServer::Stop()
{
    // 소멸자에서도 호출되므로 Start 이전/이미 Stop된 상태를 허용한다.
    m_bRunning.store(false, std::memory_order_release);

    // listener와 worker를 먼저 정지한 뒤 callback과 registry 참조를 해제한다.
    // 순서를 반대로 하면 실행 중인 /metrics handler가 이미 해제된 ServerBase나 Registry를 참조할 수 있다.
    if (m_pHttpServer != nullptr)
        m_pHttpServer->stop();

    if (m_thread.joinable())
        m_thread.join();

    m_pHttpServer.reset();
    m_beforeCollect = {};
    m_pRegistry = nullptr;
}

void MetricsHttpServer::threadProc()
{
    // cpp-httplib의 accept loop와 worker는 게임 IOCP worker와 분리되어 있다.
    // listen_after_bind는 Stop이 listener socket을 닫을 때까지 blocking하며, 반환 후 외부 상태 표시를 false로 바꾼다.
    m_pHttpServer->listen_after_bind();
    m_bRunning.store(false, std::memory_order_release);
}

void MetricsHttpServer::handleMetrics(httplib::Response& response)
{
    // 측정 범위에는 subsystem snapshot 게시와 Prometheus 문자열 직렬화가 포함된다.
    // socket 전송 시간은 cpp-httplib가 handler 반환 후 처리하므로 scrape duration에는 포함되지 않는다.
    const auto startTime = std::chrono::steady_clock::now();

    try
    {
        // Publisher들은 여기서 최신 atomic/snapshot 값을 Registry metric에 복사한다.
        // game Stage나 DB queue 전체를 직접 순회하지 않도록 각 subsystem이 저비용 snapshot 경계를 책임진다.
        // /health에는 이 callback을 실행하지 않는다. health probe 빈도가 수집 비용에 영향을 주지 않게 하기 위해서다.
        if (m_beforeCollect)
            m_beforeCollect();

        // registry rendering은 metric atomic snapshot을 읽는다. 현재 scrape의 self counter와 duration은 rendering 뒤에 갱신되므로
        // 이번 요청 결과에는 직전 scrape까지의 값이 포함되고 다음 요청부터 현재 값이 보인다.
        // callback이 값을 게시한 뒤 한 번만 text format으로 직렬화한다. response body는 cpp-httplib가 handler 반환 후 전송한다.
        const std::string body = m_pRegistry->CollectPrometheus();
        m_pRegistry->Inc(CounterMetric::MetricsHttp_ScrapesTotal);
        m_pRegistry->Set(GaugeMetric::MetricsHttp_ResponseBytes, static_cast<double>(body.size()));

        const double durationSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - startTime).count();
        m_pRegistry->Observe(HistogramMetric::MetricsHttp_ScrapeDurationSeconds, durationSeconds);
        response.set_content(body, "text/plain; version=0.0.4; charset=utf-8");
    }
    catch (...)
    {
        // monitoring 수집 예외가 HTTP worker 밖으로 전파되어 endpoint thread를 종료시키지 않게 격리한다.
        // 본 게임 서버는 계속 실행하고 Prometheus에는 500을 반환해 scrape 실패로 명확히 기록한다.
        m_pRegistry->Inc(CounterMetric::MetricsHttp_ScrapeErrorsTotal);
        response.status = httplib::StatusCode::InternalServerError_500;
        response.set_content("metrics collection failed\n", "text/plain; charset=utf-8");
    }
}

} // namespace serverbase
