#include "pch.h"
#include "DbMetricsPublisher.h"

namespace serverbase
{

DbMetricsPublisher::DbOperationMetrics* DbMetricsPublisher::findOperationMetrics(db::EDBType type, bool transaction)
{
    // 숫자 index 계산을 사용하지 않아 새 EDBType이 추가됐을 때 기존 Account/Game metric에 조용히 섞이지 않게 한다.
    // transaction bool은 같은 DB 안에서 query와 transaction metric 묶음만 선택한다.
    switch (type)
    {
    case db::EDBType::Account:
        return transaction ? &m_accountTransactionMetrics : &m_accountQueryMetrics;
    case db::EDBType::Game:
        return transaction ? &m_gameTransactionMetrics : &m_gameQueryMetrics;
    default:
        return nullptr;
    }
}

bool DbMetricsPublisher::Initialize(MetricsRegistry& registry, db::AsyncDBQueue& queue)
{
    // 짧은 query부터 장애 시의 장시간 대기까지 한 histogram으로 관찰하기 위한 고정 경계다.
    // bucket이나 label을 runtime에 늘리면 cardinality와 scrape 응답 크기가 함께 증가하므로 시작할 때만 등록한다.
    const std::vector<double> latencyBuckets = { 0.001, 0.0025, 0.005, 0.010, 0.025, 0.050, 0.100, 0.250, 0.500, 1.0, 2.5, 5.0 };

    // DB index나 SQL 문자열은 label로 사용하지 않는다. shard index/query text는 값의 종류가 계속 늘어날 수 있어
    // Prometheus cardinality를 크게 증가시키므로 DB 종류와 작업 형태의 고정 4조합만 등록한다.
    // 각 Add 호출에 label을 직접 써서 어떤 series가 만들어지는지 코드만 보고 확인할 수 있게 한다.
    bool registered = true;
    registered &= registry.AddCounter(CounterMetric::Db_AccountQuery_Requests, "mmo_db_requests_total", "DB requests accepted into the worker queue.", { { "db_type", "account" }, { "operation", "query" } });
    registered &= registry.AddCounter(CounterMetric::Db_AccountQuery_CompletedSuccess, "mmo_db_results_total", "Completed DB requests by result.", { { "db_type", "account" }, { "operation", "query" }, { "result", "success" } });
    registered &= registry.AddCounter(CounterMetric::Db_AccountQuery_CompletedBusinessFailure, "mmo_db_results_total", "Completed DB requests by result.", { { "db_type", "account" }, { "operation", "query" }, { "result", "business_failure" } });
    registered &= registry.AddCounter(CounterMetric::Db_AccountQuery_CompletedDbError, "mmo_db_results_total", "Completed DB requests by result.", { { "db_type", "account" }, { "operation", "query" }, { "result", "db_error" } });
    registered &= registry.AddCounter(CounterMetric::Db_AccountQuery_Rejected, "mmo_db_rejected_total", "DB requests rejected because the target DB was not registered.", { { "db_type", "account" }, { "operation", "query" } });
    registered &= registry.AddGauge(GaugeMetric::Db_AccountQuery_QueueDepth, "mmo_db_queue_depth", "DB requests waiting for a worker.", { { "db_type", "account" }, { "operation", "query" } });
    registered &= registry.AddGauge(GaugeMetric::Db_AccountQuery_ActiveRequests, "mmo_db_active_requests", "DB requests currently executing on workers.", { { "db_type", "account" }, { "operation", "query" } });
    registered &= registry.AddHistogram(HistogramMetric::Db_AccountQuery_QueueWaitSeconds, "mmo_db_queue_wait_seconds", "Time DB requests spent waiting for a worker.", latencyBuckets, { { "db_type", "account" }, { "operation", "query" } });
    registered &= registry.AddHistogram(HistogramMetric::Db_AccountQuery_ExecutionSeconds, "mmo_db_execution_seconds", "Time spent executing DB work on a worker.", latencyBuckets, { { "db_type", "account" }, { "operation", "query" } });

    registered &= registry.AddCounter(CounterMetric::Db_AccountTransaction_Requests, "mmo_db_requests_total", "DB requests accepted into the worker queue.", { { "db_type", "account" }, { "operation", "transaction" } });
    registered &= registry.AddCounter(CounterMetric::Db_AccountTransaction_CompletedSuccess, "mmo_db_results_total", "Completed DB requests by result.", { { "db_type", "account" }, { "operation", "transaction" }, { "result", "success" } });
    registered &= registry.AddCounter(CounterMetric::Db_AccountTransaction_CompletedBusinessFailure, "mmo_db_results_total", "Completed DB requests by result.", { { "db_type", "account" }, { "operation", "transaction" }, { "result", "business_failure" } });
    registered &= registry.AddCounter(CounterMetric::Db_AccountTransaction_CompletedDbError, "mmo_db_results_total", "Completed DB requests by result.", { { "db_type", "account" }, { "operation", "transaction" }, { "result", "db_error" } });
    registered &= registry.AddCounter(CounterMetric::Db_AccountTransaction_Rejected, "mmo_db_rejected_total", "DB requests rejected because the target DB was not registered.", { { "db_type", "account" }, { "operation", "transaction" } });
    registered &= registry.AddGauge(GaugeMetric::Db_AccountTransaction_QueueDepth, "mmo_db_queue_depth", "DB requests waiting for a worker.", { { "db_type", "account" }, { "operation", "transaction" } });
    registered &= registry.AddGauge(GaugeMetric::Db_AccountTransaction_ActiveRequests, "mmo_db_active_requests", "DB requests currently executing on workers.", { { "db_type", "account" }, { "operation", "transaction" } });
    registered &= registry.AddHistogram(HistogramMetric::Db_AccountTransaction_QueueWaitSeconds, "mmo_db_queue_wait_seconds", "Time DB requests spent waiting for a worker.", latencyBuckets, { { "db_type", "account" }, { "operation", "transaction" } });
    registered &= registry.AddHistogram(HistogramMetric::Db_AccountTransaction_ExecutionSeconds, "mmo_db_execution_seconds", "Time spent executing DB work on a worker.", latencyBuckets, { { "db_type", "account" }, { "operation", "transaction" } });

    registered &= registry.AddCounter(CounterMetric::Db_GameQuery_Requests, "mmo_db_requests_total", "DB requests accepted into the worker queue.", { { "db_type", "game" }, { "operation", "query" } });
    registered &= registry.AddCounter(CounterMetric::Db_GameQuery_CompletedSuccess, "mmo_db_results_total", "Completed DB requests by result.", { { "db_type", "game" }, { "operation", "query" }, { "result", "success" } });
    registered &= registry.AddCounter(CounterMetric::Db_GameQuery_CompletedBusinessFailure, "mmo_db_results_total", "Completed DB requests by result.", { { "db_type", "game" }, { "operation", "query" }, { "result", "business_failure" } });
    registered &= registry.AddCounter(CounterMetric::Db_GameQuery_CompletedDbError, "mmo_db_results_total", "Completed DB requests by result.", { { "db_type", "game" }, { "operation", "query" }, { "result", "db_error" } });
    registered &= registry.AddCounter(CounterMetric::Db_GameQuery_Rejected, "mmo_db_rejected_total", "DB requests rejected because the target DB was not registered.", { { "db_type", "game" }, { "operation", "query" } });
    registered &= registry.AddGauge(GaugeMetric::Db_GameQuery_QueueDepth, "mmo_db_queue_depth", "DB requests waiting for a worker.", { { "db_type", "game" }, { "operation", "query" } });
    registered &= registry.AddGauge(GaugeMetric::Db_GameQuery_ActiveRequests, "mmo_db_active_requests", "DB requests currently executing on workers.", { { "db_type", "game" }, { "operation", "query" } });
    registered &= registry.AddHistogram(HistogramMetric::Db_GameQuery_QueueWaitSeconds, "mmo_db_queue_wait_seconds", "Time DB requests spent waiting for a worker.", latencyBuckets, { { "db_type", "game" }, { "operation", "query" } });
    registered &= registry.AddHistogram(HistogramMetric::Db_GameQuery_ExecutionSeconds, "mmo_db_execution_seconds", "Time spent executing DB work on a worker.", latencyBuckets, { { "db_type", "game" }, { "operation", "query" } });

    registered &= registry.AddCounter(CounterMetric::Db_GameTransaction_Requests, "mmo_db_requests_total", "DB requests accepted into the worker queue.", { { "db_type", "game" }, { "operation", "transaction" } });
    registered &= registry.AddCounter(CounterMetric::Db_GameTransaction_CompletedSuccess, "mmo_db_results_total", "Completed DB requests by result.", { { "db_type", "game" }, { "operation", "transaction" }, { "result", "success" } });
    registered &= registry.AddCounter(CounterMetric::Db_GameTransaction_CompletedBusinessFailure, "mmo_db_results_total", "Completed DB requests by result.", { { "db_type", "game" }, { "operation", "transaction" }, { "result", "business_failure" } });
    registered &= registry.AddCounter(CounterMetric::Db_GameTransaction_CompletedDbError, "mmo_db_results_total", "Completed DB requests by result.", { { "db_type", "game" }, { "operation", "transaction" }, { "result", "db_error" } });
    registered &= registry.AddCounter(CounterMetric::Db_GameTransaction_Rejected, "mmo_db_rejected_total", "DB requests rejected because the target DB was not registered.", { { "db_type", "game" }, { "operation", "transaction" } });
    registered &= registry.AddGauge(GaugeMetric::Db_GameTransaction_QueueDepth, "mmo_db_queue_depth", "DB requests waiting for a worker.", { { "db_type", "game" }, { "operation", "transaction" } });
    registered &= registry.AddGauge(GaugeMetric::Db_GameTransaction_ActiveRequests, "mmo_db_active_requests", "DB requests currently executing on workers.", { { "db_type", "game" }, { "operation", "transaction" } });
    registered &= registry.AddHistogram(HistogramMetric::Db_GameTransaction_QueueWaitSeconds, "mmo_db_queue_wait_seconds", "Time DB requests spent waiting for a worker.", latencyBuckets, { { "db_type", "game" }, { "operation", "transaction" } });
    registered &= registry.AddHistogram(HistogramMetric::Db_GameTransaction_ExecutionSeconds, "mmo_db_execution_seconds", "Time spent executing DB work on a worker.", latencyBuckets, { { "db_type", "game" }, { "operation", "transaction" } });
    registered &= registry.AddGauge(GaugeMetric::Db_OldestQueueAgeSeconds, "mmo_db_queue_oldest_age_seconds", "Age of the oldest DB request waiting for a worker.");
    if (!registered)
        return false;

    // 모든 metric 등록이 끝난 뒤 sink를 공개한다. worker가 callback을 호출할 때 null metric을 참조하지 않게 한다.
    m_pQueue = &queue;
    m_pRegistry = &registry;
    queue.SetMetricsSink(this);
    return true;
}

void DbMetricsPublisher::Publish()
{
    // queue 전체를 순회하지 않고 mutex 아래에서 front의 enqueue 시각만 읽는다.
    // scrape 때 한 번만 실행되므로 DB 요청의 enqueue/worker 경로에는 추가 lock을 만들지 않는다.
    // depth가 0이면 queue가 0초를 반환하며, 요청이 있으면 현재 시각과 front enqueue 시각의 차이를 반환한다.
    // event callback만으로는 시간이 흐르는 동안 age가 증가하지 않으므로 scrape 직전에 다시 계산해야 한다.
    if (m_pQueue != nullptr)
        m_pRegistry->Set(GaugeMetric::Db_OldestQueueAgeSeconds, m_pQueue->GetOldestQueueAgeSeconds());
}

void DbMetricsPublisher::OnEnqueued(db::EDBType type, bool transaction)
{
    // 아래 callback들은 enqueue thread와 여러 DB worker에서 동시에 호출된다.
    // MetricCounter/Gauge/Histogram이 relaxed atomic을 사용하므로 별도 publisher lock이 필요 없다.
    DbOperationMetrics* pMetrics = findOperationMetrics(type, transaction);
    if (pMetrics == nullptr)
        return;

    m_pRegistry->Inc(pMetrics->requests);
    m_pRegistry->Add(pMetrics->queueDepth, 1.0);
}

void DbMetricsPublisher::OnStarted(db::EDBType type, bool transaction, double queueWaitSeconds)
{
    // worker가 요청을 가져간 순간 waiting에서 active로 이동한다. 두 gauge는 독립 atomic이라 scrape가 두 연산 사이에
    // 들어오면 순간적으로 합계가 한 건 차이 날 수 있지만, DB worker에 공용 lock을 추가하지 않고 다음 scrape에서 수렴한다.
    DbOperationMetrics* pMetrics = findOperationMetrics(type, transaction);
    if (pMetrics == nullptr)
        return;

    m_pRegistry->Add(pMetrics->queueDepth, -1.0);
    m_pRegistry->Add(pMetrics->active, 1.0);
    m_pRegistry->Observe(pMetrics->queueWait, queueWaitSeconds);
}

void DbMetricsPublisher::OnCompleted(db::EDBType type, bool transaction, double executionSeconds, const db::DBResult& result)
{
    // executionSeconds는 queue 대기 시간을 제외한 worker 실행 시간이다. end-to-end 지연은 두 histogram을 함께 확인한다.
    DbOperationMetrics* pMetrics = findOperationMetrics(type, transaction);
    if (pMetrics == nullptr)
        return;

    m_pRegistry->Add(pMetrics->active, -1.0);
    m_pRegistry->Observe(pMetrics->execution, executionSeconds);
    // errorCode가 없는 실패는 DB 호출 자체는 끝났지만 도메인 조건을 만족하지 못한 business failure로 구분한다.
    // DB 연결, SQL 실행 등의 실제 오류만 db_error로 집계해 장애 alert의 오탐을 줄인다.
    if (result.success)
        m_pRegistry->Inc(pMetrics->completedSuccess);
    else if (result.errorCode == 0)
        m_pRegistry->Inc(pMetrics->completedBusinessFailure);
    else
        m_pRegistry->Inc(pMetrics->completedDbError);
}

void DbMetricsPublisher::OnRejected(db::EDBType type, bool transaction)
{
    // target DB 미등록로 enqueue 전에 거절된 요청이다. requests/queue_depth에는 포함하지 않아 queue 처리량과 분리한다.
    DbOperationMetrics* pMetrics = findOperationMetrics(type, transaction);
    if (pMetrics != nullptr)
        m_pRegistry->Inc(pMetrics->rejected);
}

} // namespace serverbase
