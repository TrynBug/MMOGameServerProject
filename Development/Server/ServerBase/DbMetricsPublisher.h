#pragma once

#include "AsyncDBQueue.h"
#include "MetricsRegistry.h"

namespace serverbase
{

// AsyncDBQueue의 lifecycle event를 고정 cardinality Prometheus metric으로 변환한다.
// callback은 enqueue thread와 여러 DB worker에서 동시에 들어올 수 있으므로 별도 mutable 상태를 갱신하지 않고
// MetricsRegistry가 소유하는 atomic metric만 변경한다.
class DbMetricsPublisher final : public db::IAsyncDBMetricsSink
{
public:
    // Account/Game x query/transaction의 4개 series 묶음을 등록한 뒤 queue에 sink를 연결한다.
    bool Initialize(MetricsRegistry& registry, db::AsyncDBQueue& queue);

    // scrape 직전에 호출되어 queue front의 대기 시간처럼 event만으로 계산하기 어려운 현재 상태를 게시한다.
    void Publish();

    // 상태 전이:
    // Enqueued  : requests++, queue_depth++
    // Started   : queue_depth--, active++, queue_wait 관측
    // Completed : active--, execution 관측, result counter++
    // Rejected  : queue에 들어가지 않았으므로 rejected counter만 증가
    void OnEnqueued(db::EDBType type, bool transaction) override;
    void OnStarted(db::EDBType type, bool transaction, double queueWaitSeconds) override;
    void OnCompleted(db::EDBType type, bool transaction, double executionSeconds, const db::DBResult& result) override;
    void OnRejected(db::EDBType type, bool transaction) override;

private:
    // 동일한 db_type/operation label을 공유하는 metric enum 묶음이다.
    // requests/rejected/results는 서버 시작 이후 누적 Counter이고, queue/active는 현재 요청 수 Gauge다.
    // queueWait/execution은 서로 다른 구간의 지연 분포이므로 합치지 않는다.
    struct DbOperationMetrics
    {
        // worker queue에 정상 접수된 요청 수다. target DB 미등록으로 거절된 요청은 포함하지 않는다.
        CounterMetric requests;
        // 완료 결과를 세 종류로 고정 분류한다. SQL/error code 같은 가변값은 label로 노출하지 않는다.
        CounterMetric completedSuccess;
        CounterMetric completedBusinessFailure;
        CounterMetric completedDbError;
        // queue 진입 전에 target DB가 없어 거절된 요청 수다.
        CounterMetric rejected;
        // waiting과 executing을 분리해 worker 부족과 DB 실행 지연을 구분한다.
        GaugeMetric queueDepth;
        GaugeMetric active;
        // queue 대기 시간과 실제 worker 실행 시간을 초 단위로 관측한다.
        HistogramMetric queueWait;
        HistogramMetric execution;
    };

    // callback에 전달된 DB 종류와 작업 형태에 대응하는 metric 묶음을 반환한다.
    // 알 수 없는 EDBType은 다른 DB metric으로 잘못 집계하지 않고 nullptr로 거부한다.
    DbOperationMetrics* findOperationMetrics(db::EDBType type, bool transaction);

    // queue는 소유하지 않는다. ServerBase 종료 시 sink callback이 끝난 뒤 Publisher가 파괴된다.
    db::AsyncDBQueue* m_pQueue = nullptr;
    MetricsRegistry* m_pRegistry = nullptr;
    DbOperationMetrics m_accountQueryMetrics {
        CounterMetric::Db_AccountQuery_Requests, CounterMetric::Db_AccountQuery_CompletedSuccess,
        CounterMetric::Db_AccountQuery_CompletedBusinessFailure, CounterMetric::Db_AccountQuery_CompletedDbError,
        CounterMetric::Db_AccountQuery_Rejected, GaugeMetric::Db_AccountQuery_QueueDepth,
        GaugeMetric::Db_AccountQuery_ActiveRequests, HistogramMetric::Db_AccountQuery_QueueWaitSeconds,
        HistogramMetric::Db_AccountQuery_ExecutionSeconds };
    DbOperationMetrics m_accountTransactionMetrics {
        CounterMetric::Db_AccountTransaction_Requests, CounterMetric::Db_AccountTransaction_CompletedSuccess,
        CounterMetric::Db_AccountTransaction_CompletedBusinessFailure, CounterMetric::Db_AccountTransaction_CompletedDbError,
        CounterMetric::Db_AccountTransaction_Rejected, GaugeMetric::Db_AccountTransaction_QueueDepth,
        GaugeMetric::Db_AccountTransaction_ActiveRequests, HistogramMetric::Db_AccountTransaction_QueueWaitSeconds,
        HistogramMetric::Db_AccountTransaction_ExecutionSeconds };
    DbOperationMetrics m_gameQueryMetrics {
        CounterMetric::Db_GameQuery_Requests, CounterMetric::Db_GameQuery_CompletedSuccess,
        CounterMetric::Db_GameQuery_CompletedBusinessFailure, CounterMetric::Db_GameQuery_CompletedDbError,
        CounterMetric::Db_GameQuery_Rejected, GaugeMetric::Db_GameQuery_QueueDepth,
        GaugeMetric::Db_GameQuery_ActiveRequests, HistogramMetric::Db_GameQuery_QueueWaitSeconds,
        HistogramMetric::Db_GameQuery_ExecutionSeconds };
    DbOperationMetrics m_gameTransactionMetrics {
        CounterMetric::Db_GameTransaction_Requests, CounterMetric::Db_GameTransaction_CompletedSuccess,
        CounterMetric::Db_GameTransaction_CompletedBusinessFailure, CounterMetric::Db_GameTransaction_CompletedDbError,
        CounterMetric::Db_GameTransaction_Rejected, GaugeMetric::Db_GameTransaction_QueueDepth,
        GaugeMetric::Db_GameTransaction_ActiveRequests, HistogramMetric::Db_GameTransaction_QueueWaitSeconds,
        HistogramMetric::Db_GameTransaction_ExecutionSeconds };
};

} // namespace serverbase
