#pragma once

#include "MetricEnums.h"
#include "Types.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace serverbase
{

// 하나의 Prometheus series에 항상 붙는 고정 label이다.
// 예를 들어 name이 같아도 {result="success"}와 {result="failure"}는 서로 다른 두 series가 된다.
// label 조합마다 Prometheus 저장 공간과 scrape 출력 한 줄이 추가되므로 user/stage instance id처럼
// 값이 계속 늘어나는 정보는 넣지 않고, 서버 시작 시 범위를 알 수 있는 분류값에만 사용한다.
struct MetricLabel
{
    std::string name;
    std::string value;
};

// Counter/Gauge/Histogram이 공통으로 사용하는 metric family 정보와 text 변환 기능을 보관한다.
// 객체는 MetricsRegistry가 소유하며, 등록 이후 이름·HELP·label은 바뀌지 않고 값만 atomic으로 갱신된다.
class MetricBase
{
public:
    MetricBase(std::string name, std::string help, std::vector<MetricLabel> labels);
    virtual ~MetricBase() = default;

    MetricBase(const MetricBase&) = delete;
    MetricBase& operator=(const MetricBase&) = delete;

    const std::string& GetName() const { return m_name; }
    const std::string& GetHelp() const { return m_help; }

    virtual const char* GetTypeName() const = 0;
    virtual void AppendPrometheus(std::string& outText, bool includeHeader) const = 0;

protected:
    // 동일 family의 첫 series에서만 # HELP와 # TYPE을 출력한다.
    void appendHeader(std::string& outText, const char* typeName) const;
    // metric 이름과 고정 label을 출력한다. Histogram은 suffix와 임시 le label을 추가해 재사용한다.
    void appendSeriesPrefix(std::string& outText, const std::string& suffix = {}, const MetricLabel* pExtraLabel = nullptr) const;

private:
    std::string m_name;
    std::string m_help;
    std::vector<MetricLabel> m_labels;
};

// 프로세스가 살아 있는 동안 단조 증가하는 누적값이다.
// 서버 재시작 시 0으로 돌아가는 것은 정상이며 Prometheus의 rate()/increase()가 reset을 처리한다.
// Inc는 event 발생 지점에서 사용하고, Set은 NetLib/Windows처럼 외부에서 이미 누적한 절대값을 scrape 시 동기화할 때 사용한다.
class MetricCounter final : public MetricBase
{
public:
    MetricCounter(std::string name, std::string help, std::vector<MetricLabel> labels);

    void Inc(uint64 delta = 1) { m_value.fetch_add(delta, std::memory_order_relaxed); }
    void Set(uint64 value) { m_value.store(value, std::memory_order_relaxed); }
    uint64 Get() const { return m_value.load(std::memory_order_relaxed); }

    const char* GetTypeName() const override { return "counter"; }
    void AppendPrometheus(std::string& outText, bool includeHeader) const override;

private:
    std::atomic<uint64> m_value { 0 };
};

// 증가와 감소가 모두 가능한 현재 상태값이다. queue depth, active session, byte 사용량 등에 사용한다.
// Gauge를 여러 thread가 Add로 바꿀 때 개별 연산은 atomic이지만 관련 Gauge 여러 개가 동시에 바뀌는 transaction은 아니다.
// 따라서 scrape가 상태 전이 중간에 겹치면 잠깐 불일치할 수 있으며 다음 scrape에서 정상값으로 수렴한다.
class MetricGauge final : public MetricBase
{
public:
    MetricGauge(std::string name, std::string help, std::vector<MetricLabel> labels);

    void Set(double value) { m_value.store(value, std::memory_order_relaxed); }
    void Add(double delta) { m_value.fetch_add(delta, std::memory_order_relaxed); }
    double Get() const { return m_value.load(std::memory_order_relaxed); }

    const char* GetTypeName() const override { return "gauge"; }
    void AppendPrometheus(std::string& outText, bool includeHeader) const override;

private:
    std::atomic<double> m_value { 0.0 };
};

// 관측값의 분포를 고정 bucket, 전체 count, sum으로 기록한다.
// Observe는 여러 worker에서 동시에 호출할 수 있고, bucket 경계는 생성 후 변경하지 않는다.
// bucket 하나마다 별도 series가 생기므로 세밀한 경계를 과도하게 추가하면 response 크기와 Prometheus 저장량이 증가한다.
// percentile은 서버가 계산하지 않고 PromQL의 histogram_quantile()로 계산한다.
class MetricHistogram final : public MetricBase
{
public:
    MetricHistogram(std::string name, std::string help, std::vector<MetricLabel> labels, std::vector<double> bucketUpperBounds);

    void Observe(double value);

    const char* GetTypeName() const override { return "histogram"; }
    void AppendPrometheus(std::string& outText, bool includeHeader) const override;

private:
    // 내부 bucket count는 누적값이 아니라 해당 구간에 들어온 개수다.
    // Collect 시 앞 bucket부터 합산해 Prometheus가 요구하는 cumulative _bucket series로 만든다.
    std::vector<double> m_bucketUpperBounds;
    std::unique_ptr<std::atomic<uint64>[]> m_bucketCounts;
    std::atomic<uint64> m_count { 0 };
    std::atomic<double> m_sum { 0.0 };
};

// 프로세스 내 모든 metric 객체의 소유자이자 Prometheus text exposition 생성기다.
//
// 수명 규칙:
// - 외부 코드는 metric pointer를 보관하지 않고 값 타입별 enum으로만 접근한다.
// - metric 등록은 보통 서버 초기화 thread에서 한 번 수행한다.
// - 등록 후 Inc/Set/Add/Observe는 atomic이므로 여러 IO/DB/content thread에서 동시에 호출할 수 있다.
// - CollectPrometheus는 monitoring worker에서 호출되며 registry 구조만 잠근다.
class MetricsRegistry
{
public:
    MetricsRegistry() = default;
    ~MetricsRegistry() = default;

    MetricsRegistry(const MetricsRegistry&) = delete;
    MetricsRegistry& operator=(const MetricsRegistry&) = delete;

    // enum 기반 고정 series 등록 API다. enum slot과 Prometheus metric 객체를 Registry 내부에서 함께 관리한다.
    // 같은 enum의 중복 등록, 범위 밖 enum, 잘못된 name/label/family 정의는 false를 반환한다.
    bool AddCounter(CounterMetric metric, const std::string& name, const std::string& help,
        const std::vector<MetricLabel>& labels = {});
    bool AddGauge(GaugeMetric metric, const std::string& name, const std::string& help,
        const std::vector<MetricLabel>& labels = {});
    bool AddHistogram(HistogramMetric metric, const std::string& name, const std::string& help,
        const std::vector<double>& bucketUpperBounds, const std::vector<MetricLabel>& labels = {});

    // 등록되지 않은 enum은 monitoring disabled 상태로 보고 no-op 처리한다. 호출부에는 null 체크가 필요 없다.
    void Inc(CounterMetric metric, uint64 delta = 1);
    // NetLib/Windows처럼 다른 subsystem이 이미 누적한 절대 Counter를 동기화할 때 사용한다.
    void Set(CounterMetric metric, uint64 value);
    void Set(GaugeMetric metric, double value);
    void Add(GaugeMetric metric, double delta);
    void Observe(HistogramMetric metric, double value);

    // 현재 atomic 값들을 읽어 Prometheus text format 전체 응답을 생성한다.
    // 여러 metric의 값이 하나의 전역 원자적 시점에 속한다는 보장은 없으며 모니터링용 근사 snapshot으로 사용한다.
    std::string CollectPrometheus() const;

private:
    struct FamilyDefinition
    {
        // family는 label을 제외한 metric name 단위다. 같은 family의 모든 series는 type과 HELP가 반드시 같아야 한다.
        std::string type;
        std::string help;
    };

    MetricCounter* addCounter(const std::string& name, const std::string& help, const std::vector<MetricLabel>& labels);
    MetricGauge* addGauge(const std::string& name, const std::string& help, const std::vector<MetricLabel>& labels);
    MetricHistogram* addHistogram(const std::string& name, const std::string& help,
        const std::vector<double>& bucketUpperBounds, const std::vector<MetricLabel>& labels);

    bool canAddMetric(const std::string& name, const std::string& help, const char* typeName, const std::vector<MetricLabel>& labels,
        std::string& outSeriesKey);
    static bool isValidMetricName(const std::string& name);
    static bool isValidLabelName(const std::string& name);
    static bool areValidHistogramBounds(const std::vector<double>& bucketUpperBounds);
    static std::string makeSeriesKey(const std::string& name, const std::vector<MetricLabel>& labels);

private:
    // m_mutex는 metric 객체 목록과 family/series 등록 정보만 보호한다.
    // 개별 metric 값 갱신은 이 mutex와 무관하므로 game/network hot path가 scrape에 막히지 않는다.
    mutable std::mutex m_mutex;
    std::array<MetricCounter*, static_cast<size_t>(CounterMetric::_End)> m_counterMetrics{};
    std::array<MetricGauge*, static_cast<size_t>(GaugeMetric::_End)> m_gaugeMetrics{};
    std::array<MetricHistogram*, static_cast<size_t>(HistogramMetric::_End)> m_histogramMetrics{};
    // 등록 순서를 유지해 사람이 /metrics를 읽을 때 publisher별 metric이 함께 보이게 한다.
    std::vector<std::unique_ptr<MetricBase>> m_metrics;
    // family 정의는 동일 name에 type/HELP가 다르게 등록되는 오류를 시작 단계에서 검출한다.
    std::unordered_map<std::string, FamilyDefinition> m_families;
    // name과 정규화한 label 조합의 중복 등록을 검출한다. label 순서가 달라도 같은 key로 취급한다.
    std::unordered_set<std::string> m_seriesKeys;
};

} // namespace serverbase
