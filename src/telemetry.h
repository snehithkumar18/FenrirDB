#ifndef FENRIRDB_TELEMETRY_H
#define FENRIRDB_TELEMETRY_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace FenrirDB {

enum class MetricKind : uint8_t {
    COUNTER = 1,
    GAUGE = 2,
    HISTOGRAM = 3
};

struct MetricSample {
    std::string name;
    MetricKind kind = MetricKind::COUNTER;
    uint64_t timestamp_ms = 0;
    double value = 0.0;
    std::map<std::string, std::string> labels;
};

struct HistogramBucket {
    double upper_bound = 0.0;
    uint64_t count = 0;
};

struct HistogramState {
    std::string name;
    std::vector<HistogramBucket> buckets;
    uint64_t overflow_count = 0;
    double sum = 0.0;
};

struct TraceSpan {
    uint64_t span_id = 0;
    uint64_t parent_span_id = 0;
    std::string operation;
    uint64_t start_ms = 0;
    uint64_t end_ms = 0;
    std::map<std::string, std::string> attributes;
};

struct TelemetryReport {
    std::vector<MetricSample> counters;
    std::vector<MetricSample> gauges;
    std::vector<HistogramState> histograms;
    std::vector<TraceSpan> slow_spans;
    std::string summary() const;
};

class TelemetryRegistry {
public:
    void add_counter(const std::string& name, double value,
                     const std::map<std::string, std::string>& labels,
                     uint64_t timestamp_ms);
    void set_gauge(const std::string& name, double value,
                   const std::map<std::string, std::string>& labels,
                   uint64_t timestamp_ms);
    void observe_histogram(const std::string& name, double value,
                           const std::vector<double>& bounds,
                           uint64_t timestamp_ms);
    uint64_t start_span(const std::string& operation, uint64_t parent_span_id,
                        uint64_t start_ms);
    void finish_span(uint64_t span_id, uint64_t end_ms,
                     const std::map<std::string, std::string>& attributes);

    TelemetryReport snapshot(uint64_t slow_threshold_ms) const;
    void merge(const TelemetryRegistry& other);
    void reset();

private:
    std::vector<MetricSample> samples;
    std::map<std::string, HistogramState> histograms;
    std::map<uint64_t, TraceSpan> spans;
    uint64_t next_span_id = 1;

    static std::string metric_key(const std::string& name,
                                  const std::map<std::string, std::string>& labels);
};

class TelemetryParser {
public:
    bool parse_text(const std::string& text, TelemetryRegistry& registry) const;
    std::string emit_report(const TelemetryReport& report) const;

private:
    static std::map<std::string, std::string> parse_labels(const std::string& text);
    static std::vector<double> parse_bounds(const std::string& text);
};

} // namespace FenrirDB

#endif // FENRIRDB_TELEMETRY_H
