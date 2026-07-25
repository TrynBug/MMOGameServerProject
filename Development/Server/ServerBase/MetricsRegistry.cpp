#include "pch.h"
#include "MetricsRegistry.h"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace serverbase
{

namespace
{
    // Prometheus text format에서 HELP와 label value가 요구하는 escape 규칙이 서로 달라 별도 함수로 처리한다.
    // HELP에는 backslash와 newline만 escape하고, label value에는 double quote까지 escape해야 한다.
    void AppendEscapedHelp(std::string& outText, const std::string& value)
    {
        for (char ch : value)
        {
            if (ch == '\\')
                outText += "\\\\";
            else if (ch == '\n')
                outText += "\\n";
            else
                outText += ch;
        }
    }

    void AppendEscapedLabelValue(std::string& outText, const std::string& value)
    {
        for (char ch : value)
        {
            if (ch == '\\')
                outText += "\\\\";
            else if (ch == '"')
                outText += "\\\"";
            else if (ch == '\n')
                outText += "\\n";
            else
                outText += ch;
        }
    }

    void AppendDouble(std::string& outText, double value)
    {
        // locale 영향을 받는 stream 대신 to_chars를 사용해 소수점이 항상 Prometheus 형식으로 출력되게 한다.
        // NaN과 infinity는 Prometheus/OpenMetrics가 이해하는 명시적 문자열로 변환한다.
        if (std::isnan(value))
        {
            outText += "NaN";
            return;
        }

        if (std::isinf(value))
        {
            outText += (value > 0.0) ? "+Inf" : "-Inf";
            return;
        }

        char buffer[64]{};
        const std::to_chars_result result = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general);
        if (result.ec == std::errc())
            outText.append(buffer, result.ptr);
        else
            outText += "NaN";
    }
}

MetricBase::MetricBase(std::string name, std::string help, std::vector<MetricLabel> labels)
    : m_name(std::move(name)), m_help(std::move(help)), m_labels(std::move(labels))
{
}

void MetricBase::appendHeader(std::string& outText, const char* typeName) const
{
    // 같은 metric name에 label만 다른 series가 여러 개 있어도 이 두 줄은 family당 한 번만 필요하다.
    // family 중 첫 객체인지 여부는 CollectPrometheus가 includeHeader로 전달한다.
    outText += "# HELP ";
    outText += m_name;
    outText += ' ';
    AppendEscapedHelp(outText, m_help);
    outText += '\n';
    outText += "# TYPE ";
    outText += m_name;
    outText += ' ';
    outText += typeName;
    outText += '\n';
}

void MetricBase::appendSeriesPrefix(std::string& outText, const std::string& suffix, const MetricLabel* pExtraLabel) const
{
    // 일반 metric은 "name{fixed_labels}"를, Histogram은 "name_bucket{fixed_labels,le=...}" 등을 만든다.
    // pExtraLabel은 출력 시에만 사용되며 등록된 고정 label이나 series identity를 변경하지 않는다.
    outText += m_name;
    outText += suffix;

    if (m_labels.empty() && pExtraLabel == nullptr)
        return;

    outText += '{';
    bool first = true;
    for (const MetricLabel& label : m_labels)
    {
        if (!first)
            outText += ',';

        outText += label.name;
        outText += "=\"";
        AppendEscapedLabelValue(outText, label.value);
        outText += '"';
        first = false;
    }

    if (pExtraLabel != nullptr)
    {
        if (!first)
            outText += ',';

        outText += pExtraLabel->name;
        outText += "=\"";
        AppendEscapedLabelValue(outText, pExtraLabel->value);
        outText += '"';
    }

    outText += '}';
}

MetricCounter::MetricCounter(std::string name, std::string help, std::vector<MetricLabel> labels)
    : MetricBase(std::move(name), std::move(help), std::move(labels))
{
}

void MetricCounter::AppendPrometheus(std::string& outText, bool includeHeader) const
{
    // 같은 family의 첫 series만 metadata를 쓰고 모든 series는 각각 value line 하나를 출력한다.
    // Counter의 uint64 snapshot은 정수 문자열로 출력해 큰 누적값에서 불필요한 부동소수점 오차를 만들지 않는다.
    if (includeHeader)
        appendHeader(outText, GetTypeName());

    appendSeriesPrefix(outText);
    outText += ' ';
    outText += std::to_string(Get());
    outText += '\n';
}

MetricGauge::MetricGauge(std::string name, std::string help, std::vector<MetricLabel> labels)
    : MetricBase(std::move(name), std::move(help), std::move(labels))
{
}

void MetricGauge::AppendPrometheus(std::string& outText, bool includeHeader) const
{
    // Gauge는 byte/count뿐 아니라 초 단위 소수값도 담으므로 locale 독립적인 AppendDouble을 사용한다.
    if (includeHeader)
        appendHeader(outText, GetTypeName());

    appendSeriesPrefix(outText);
    outText += ' ';
    AppendDouble(outText, Get());
    outText += '\n';
}

MetricHistogram::MetricHistogram(std::string name, std::string help, std::vector<MetricLabel> labels, std::vector<double> bucketUpperBounds)
    : MetricBase(std::move(name), std::move(help), std::move(labels)),
      m_bucketUpperBounds(std::move(bucketUpperBounds)),
      m_bucketCounts(std::make_unique<std::atomic<uint64>[]>(m_bucketUpperBounds.size()))
{
    for (size_t i = 0; i < m_bucketUpperBounds.size(); ++i)
        m_bucketCounts[i].store(0, std::memory_order_relaxed);
}

void MetricHistogram::Observe(double value)
{
    if (std::isnan(value))
        return;

    // Observe에서는 처음 포함되는 bucket 하나만 증가시킨다. hot path의 atomic 증가 횟수를 줄이고,
    // scrape 출력 시 앞에서부터 누적해 Prometheus의 cumulative bucket 형식으로 변환한다.
    const auto iter = std::lower_bound(m_bucketUpperBounds.begin(), m_bucketUpperBounds.end(), value);
    if (iter != m_bucketUpperBounds.end())
    {
        const size_t index = static_cast<size_t>(std::distance(m_bucketUpperBounds.begin(), iter));
        m_bucketCounts[index].fetch_add(1, std::memory_order_relaxed);
    }

    m_count.fetch_add(1, std::memory_order_relaxed);
    m_sum.fetch_add(value, std::memory_order_relaxed);
    // bucket/count/sum은 서로 독립된 atomic이다. scrape가 Observe와 정확히 겹치면 한 요청 안에서 잠깐 차이가 날 수 있지만,
    // 다음 scrape에서 수렴하며 hot path에 global lock을 두지 않는 것을 우선한다.
}

void MetricHistogram::AppendPrometheus(std::string& outText, bool includeHeader) const
{
    if (includeHeader)
        appendHeader(outText, GetTypeName());

    // 내부에는 관측값이 처음 들어간 구간 하나만 증가시켜 두었으므로, 여기서 누적합을 만들어
    // 각 le bucket이 "이 경계 이하인 전체 관측 수"라는 Prometheus histogram 규약을 만족시킨다.
    uint64 cumulativeCount = 0;
    for (size_t i = 0; i < m_bucketUpperBounds.size(); ++i)
    {
        cumulativeCount += m_bucketCounts[i].load(std::memory_order_relaxed);

        std::string boundary;
        AppendDouble(boundary, m_bucketUpperBounds[i]);
        const MetricLabel boundaryLabel { "le", std::move(boundary) };
        appendSeriesPrefix(outText, "_bucket", &boundaryLabel);
        outText += ' ';
        outText += std::to_string(cumulativeCount);
        outText += '\n';
    }

    // 명시된 마지막 경계보다 큰 관측값은 개별 bucket에 저장되지 않지만 전체 count에는 포함된다.
    // 따라서 +Inf bucket에는 항상 전체 count를 사용한다.
    const uint64 count = m_count.load(std::memory_order_relaxed);
    const double sum = m_sum.load(std::memory_order_relaxed);

    const MetricLabel infinityLabel { "le", "+Inf" };
    appendSeriesPrefix(outText, "_bucket", &infinityLabel);
    outText += ' ';
    outText += std::to_string(count);
    outText += '\n';

    appendSeriesPrefix(outText, "_sum");
    outText += ' ';
    AppendDouble(outText, sum);
    outText += '\n';

    appendSeriesPrefix(outText, "_count");
    outText += ' ';
    outText += std::to_string(count);
    outText += '\n';
}

bool MetricsRegistry::AddCounter(CounterMetric metric, const std::string& name, const std::string& help,
    const std::vector<MetricLabel>& labels)
{
    const size_t index = static_cast<size_t>(metric);
    if (index >= m_counterMetrics.size() || m_counterMetrics[index] != nullptr)
        return false;

    MetricCounter* pMetric = addCounter(name, help, labels);
    if (pMetric == nullptr)
        return false;

    m_counterMetrics[index] = pMetric;
    return true;
}

bool MetricsRegistry::AddGauge(GaugeMetric metric, const std::string& name, const std::string& help,
    const std::vector<MetricLabel>& labels)
{
    const size_t index = static_cast<size_t>(metric);
    if (index >= m_gaugeMetrics.size() || m_gaugeMetrics[index] != nullptr)
        return false;

    MetricGauge* pMetric = addGauge(name, help, labels);
    if (pMetric == nullptr)
        return false;

    m_gaugeMetrics[index] = pMetric;
    return true;
}

bool MetricsRegistry::AddHistogram(HistogramMetric metric, const std::string& name, const std::string& help,
    const std::vector<double>& bucketUpperBounds, const std::vector<MetricLabel>& labels)
{
    const size_t index = static_cast<size_t>(metric);
    if (index >= m_histogramMetrics.size() || m_histogramMetrics[index] != nullptr)
        return false;

    MetricHistogram* pMetric = addHistogram(name, help, bucketUpperBounds, labels);
    if (pMetric == nullptr)
        return false;

    m_histogramMetrics[index] = pMetric;
    return true;
}

void MetricsRegistry::Inc(CounterMetric metric, uint64 delta)
{
    const size_t index = static_cast<size_t>(metric);
    if (index < m_counterMetrics.size() && m_counterMetrics[index] != nullptr)
        m_counterMetrics[index]->Inc(delta);
}

void MetricsRegistry::Set(CounterMetric metric, uint64 value)
{
    const size_t index = static_cast<size_t>(metric);
    if (index < m_counterMetrics.size() && m_counterMetrics[index] != nullptr)
        m_counterMetrics[index]->Set(value);
}

void MetricsRegistry::Set(GaugeMetric metric, double value)
{
    const size_t index = static_cast<size_t>(metric);
    if (index < m_gaugeMetrics.size() && m_gaugeMetrics[index] != nullptr)
        m_gaugeMetrics[index]->Set(value);
}

void MetricsRegistry::Add(GaugeMetric metric, double delta)
{
    const size_t index = static_cast<size_t>(metric);
    if (index < m_gaugeMetrics.size() && m_gaugeMetrics[index] != nullptr)
        m_gaugeMetrics[index]->Add(delta);
}

void MetricsRegistry::Observe(HistogramMetric metric, double value)
{
    const size_t index = static_cast<size_t>(metric);
    if (index < m_histogramMetrics.size() && m_histogramMetrics[index] != nullptr)
        m_histogramMetrics[index]->Observe(value);
}

MetricCounter* MetricsRegistry::addCounter(const std::string& name, const std::string& help, const std::vector<MetricLabel>& labels)
{
    // 등록과 collection만 registry mutex를 사용한다. 등록된 metric의 Inc/Set/Observe는 atomic이므로 이 lock을 잡지 않는다.
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string seriesKey;
    if (!canAddMetric(name, help, "counter", labels, seriesKey))
        return nullptr;

    // metric 객체는 unique_ptr이 소유하므로 vector가 재할당돼도 enum series 배열이 가리키는 주소는 바뀌지 않는다.
    auto spMetric = std::make_unique<MetricCounter>(name, help, labels);
    MetricCounter* pMetric = spMetric.get();
    m_metrics.push_back(std::move(spMetric));
    m_seriesKeys.insert(std::move(seriesKey));
    return pMetric;
}

MetricGauge* MetricsRegistry::addGauge(const std::string& name, const std::string& help, const std::vector<MetricLabel>& labels)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string seriesKey;
    if (!canAddMetric(name, help, "gauge", labels, seriesKey))
        return nullptr;

    auto spMetric = std::make_unique<MetricGauge>(name, help, labels);
    MetricGauge* pMetric = spMetric.get();
    m_metrics.push_back(std::move(spMetric));
    m_seriesKeys.insert(std::move(seriesKey));
    return pMetric;
}

MetricHistogram* MetricsRegistry::addHistogram(const std::string& name, const std::string& help, const std::vector<double>& bucketUpperBounds,
    const std::vector<MetricLabel>& labels)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string seriesKey;
    // 경계가 비어 있거나 오름차순이 아니면 lower_bound와 cumulative 출력의 의미가 깨지므로 등록을 거부한다.
    if (!areValidHistogramBounds(bucketUpperBounds) || !canAddMetric(name, help, "histogram", labels, seriesKey))
        return nullptr;

    auto spMetric = std::make_unique<MetricHistogram>(name, help, labels, bucketUpperBounds);
    MetricHistogram* pMetric = spMetric.get();
    m_metrics.push_back(std::move(spMetric));
    m_seriesKeys.insert(std::move(seriesKey));
    return pMetric;
}

std::string MetricsRegistry::CollectPrometheus() const
{
    // metric 추가와 동시에 vector를 순회하지 않도록 registry 구조만 잠근다.
    // 각 metric 값은 relaxed atomic snapshot이므로 서로 완전히 같은 시점일 필요가 없는 관측 데이터로 취급한다.
    std::lock_guard<std::mutex> lock(m_mutex);

    std::string output;
    output.reserve(m_metrics.size() * 128);

    // m_metrics는 series 단위 목록이고 HELP/TYPE은 family 단위 정보다.
    // 첫 series를 만났을 때만 header를 출력해 같은 metric name의 label별 series를 한 family로 묶는다.
    std::unordered_set<std::string> emittedFamilies;
    emittedFamilies.reserve(m_families.size());
    for (const std::unique_ptr<MetricBase>& spMetric : m_metrics)
    {
        // 같은 이름과 다른 고정 label을 가진 series는 하나의 family다. HELP/TYPE header는 family당 한 번만 출력한다.
        const bool includeHeader = emittedFamilies.insert(spMetric->GetName()).second;
        spMetric->AppendPrometheus(output, includeHeader);
    }

    return output;
}

bool MetricsRegistry::canAddMetric(const std::string& name, const std::string& help, const char* typeName,
    const std::vector<MetricLabel>& labels, std::string& outSeriesKey)
{
    // 등록 단계에서 exposition 구조 오류를 차단한다. 실행 중 scrape에서는 재검증하지 않아 응답 생성 비용을 줄인다.
    if (!isValidMetricName(name) || help.empty())
        return false;

    // 같은 series 안에서 label name이 중복되면 Prometheus parser가 의미를 결정할 수 없으므로 거부한다.
    // label value는 빈 문자열도 유효하며 exposition 출력 단계에서 escape한다.
    std::unordered_set<std::string> labelNames;
    for (const MetricLabel& label : labels)
    {
        if (!isValidLabelName(label.name) || !labelNames.insert(label.name).second)
            return false;
    }

    auto familyIter = m_families.find(name);
    if (familyIter == m_families.end())
    {
        m_families.emplace(name, FamilyDefinition { typeName, help });
    }
    else if (familyIter->second.type != typeName || familyIter->second.help != help)
    {
        // Prometheus family 하나에 type이나 HELP가 다르면 exposition 자체가 모호해지므로 등록 단계에서 거부한다.
        return false;
    }

    // label 순서만 다른 동일 series도 중복으로 판단한다. caller는 고정 label만 시작 시 등록해 cardinality를 관리한다.
    outSeriesKey = makeSeriesKey(name, labels);
    return !m_seriesKeys.contains(outSeriesKey);
}

bool MetricsRegistry::isValidMetricName(const std::string& name)
{
    if (name.empty())
        return false;

    const auto isFirstChar = [](char ch)
    {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_' || ch == ':';
    };
    const auto isOtherChar = [&isFirstChar](char ch)
    {
        return isFirstChar(ch) || (ch >= '0' && ch <= '9');
    };

    if (!isFirstChar(name.front()))
        return false;

    return std::all_of(name.begin() + 1, name.end(), isOtherChar);
}

bool MetricsRegistry::isValidLabelName(const std::string& name)
{
    if (name.empty())
        return false;

    const auto isFirstChar = [](char ch)
    {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
    };
    const auto isOtherChar = [&isFirstChar](char ch)
    {
        return isFirstChar(ch) || (ch >= '0' && ch <= '9');
    };

    if (!isFirstChar(name.front()))
        return false;

    return std::all_of(name.begin() + 1, name.end(), isOtherChar);
}

bool MetricsRegistry::areValidHistogramBounds(const std::vector<double>& bucketUpperBounds)
{
    if (bucketUpperBounds.empty())
        return false;

    for (size_t i = 0; i < bucketUpperBounds.size(); ++i)
    {
        // +Inf bucket은 AppendPrometheus가 자동 생성하므로 caller 경계에는 유한값만 허용한다.
        if (!std::isfinite(bucketUpperBounds[i]))
            return false;
        if (i > 0 && bucketUpperBounds[i - 1] >= bucketUpperBounds[i])
            return false;
    }

    return true;
}

std::string MetricsRegistry::makeSeriesKey(const std::string& name, const std::vector<MetricLabel>& labels)
{
    std::string key = name;
    std::vector<const MetricLabel*> sortedLabels;
    sortedLabels.reserve(labels.size());
    for (const MetricLabel& label : labels)
        sortedLabels.push_back(&label);

    // caller가 label을 넘긴 순서와 무관하게 같은 name/value 조합이 같은 key가 되도록 이름순으로 정규화한다.
    std::sort(sortedLabels.begin(), sortedLabels.end(), [](const MetricLabel* pLeft, const MetricLabel* pRight)
        {
            return pLeft->name < pRight->name;
        });

    for (const MetricLabel* pLabel : sortedLabels)
    {
        key += '\n';
        key += pLabel->name;
        key += '=';
        key += pLabel->value;
    }
    return key;
}

} // namespace serverbase
