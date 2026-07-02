#include "telemetry.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace FenrirDB {

namespace {

std::string trim(const std::string& input) {
    size_t begin = 0;
    while (begin < input.size() && std::isspace(static_cast<unsigned char>(input[begin]))) ++begin;
    size_t end = input.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1]))) --end;
    return input.substr(begin, end - begin);
}

std::vector<std::string> split(const std::string& text, char delim) {
    std::vector<std::string> out;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, delim)) {
        item = trim(item);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

std::string labels_text(const std::map<std::string, std::string>& labels) {
    std::ostringstream out;
    bool first = true;
    for (const auto& pair : labels) {
        if (!first) out << ",";
        first = false;
        out << pair.first << "=" << pair.second;
    }
    return out.str();
}

} // namespace

std::string TelemetryReport::summary() const {
    std::ostringstream out;
    out << "counters=" << counters.size() << " gauges=" << gauges.size()
        << " histograms=" << histograms.size() << " slow_spans=" << slow_spans.size() << "\n";
    for (const auto& sample : counters) {
        out << "counter " << sample.name << " value=" << sample.value
            << " labels=" << labels_text(sample.labels) << "\n";
    }
    for (const auto& sample : gauges) {
        out << "gauge " << sample.name << " value=" << sample.value
            << " labels=" << labels_text(sample.labels) << "\n";
    }
    for (const auto& histogram : histograms) {
        out << "histogram " << histogram.name << " sum=" << histogram.sum
            << " overflow=" << histogram.overflow_count;
        for (const auto& bucket : histogram.buckets) {
            out << " le_" << bucket.upper_bound << "=" << bucket.count;
        }
        out << "\n";
    }
    for (const auto& span : slow_spans) {
        out << "span " << span.span_id << " op=" << span.operation
            << " duration=" << (span.end_ms >= span.start_ms ? span.end_ms - span.start_ms : 0)
            << " parent=" << span.parent_span_id << "\n";
    }
    return out.str();
}

void TelemetryRegistry::add_counter(const std::string& name, double value,
                                    const std::map<std::string, std::string>& labels,
                                    uint64_t timestamp_ms) {
    MetricSample sample;
    sample.name = name;
    sample.kind = MetricKind::COUNTER;
    sample.timestamp_ms = timestamp_ms;
    sample.value = value;
    sample.labels = labels;
    samples.push_back(sample);
}

void TelemetryRegistry::set_gauge(const std::string& name, double value,
                                  const std::map<std::string, std::string>& labels,
                                  uint64_t timestamp_ms) {
    std::string key = metric_key(name, labels);
    for (auto& sample : samples) {
        if (sample.kind == MetricKind::GAUGE && metric_key(sample.name, sample.labels) == key) {
            sample.value = value;
            sample.timestamp_ms = timestamp_ms;
            return;
        }
    }
    MetricSample sample;
    sample.name = name;
    sample.kind = MetricKind::GAUGE;
    sample.timestamp_ms = timestamp_ms;
    sample.value = value;
    sample.labels = labels;
    samples.push_back(sample);
}

void TelemetryRegistry::observe_histogram(const std::string& name, double value,
                                          const std::vector<double>& bounds,
                                          uint64_t) {
    HistogramState& state = histograms[name];
    state.name = name;
    if (state.buckets.empty()) {
        std::vector<double> sorted = bounds;
        std::sort(sorted.begin(), sorted.end());
        for (double bound : sorted) {
            state.buckets.push_back({bound, 0});
        }
    }
    state.sum += value;
    bool placed = false;
    for (auto& bucket : state.buckets) {
        if (value <= bucket.upper_bound) {
            ++bucket.count;
            placed = true;
            break;
        }
    }
    if (!placed) ++state.overflow_count;
}

uint64_t TelemetryRegistry::start_span(const std::string& operation, uint64_t parent_span_id,
                                       uint64_t start_ms) {
    TraceSpan span;
    span.span_id = next_span_id++;
    span.parent_span_id = parent_span_id;
    span.operation = operation;
    span.start_ms = start_ms;
    spans[span.span_id] = span;
    return span.span_id;
}

void TelemetryRegistry::finish_span(uint64_t span_id, uint64_t end_ms,
                                    const std::map<std::string, std::string>& attributes) {
    auto it = spans.find(span_id);
    if (it == spans.end()) return;
    it->second.end_ms = end_ms;
    for (const auto& pair : attributes) {
        it->second.attributes[pair.first] = pair.second;
    }
}

TelemetryReport TelemetryRegistry::snapshot(uint64_t slow_threshold_ms) const {
    TelemetryReport report;
    for (const auto& sample : samples) {
        if (sample.kind == MetricKind::COUNTER) report.counters.push_back(sample);
        else if (sample.kind == MetricKind::GAUGE) report.gauges.push_back(sample);
    }
    for (const auto& pair : histograms) {
        report.histograms.push_back(pair.second);
    }
    for (const auto& pair : spans) {
        const TraceSpan& span = pair.second;
        uint64_t duration = span.end_ms >= span.start_ms ? span.end_ms - span.start_ms : 0;
        if (span.end_ms != 0 && duration >= slow_threshold_ms) {
            report.slow_spans.push_back(span);
        }
    }
    return report;
}

void TelemetryRegistry::merge(const TelemetryRegistry& other) {
    for (const auto& sample : other.samples) {
        if (sample.kind == MetricKind::COUNTER) add_counter(sample.name, sample.value, sample.labels, sample.timestamp_ms);
        else if (sample.kind == MetricKind::GAUGE) set_gauge(sample.name, sample.value, sample.labels, sample.timestamp_ms);
    }
    for (const auto& pair : other.histograms) {
        HistogramState& local = histograms[pair.first];
        if (local.buckets.empty()) local = pair.second;
        else {
            local.sum += pair.second.sum;
            local.overflow_count += pair.second.overflow_count;
            for (size_t i = 0; i < local.buckets.size() && i < pair.second.buckets.size(); ++i) {
                local.buckets[i].count += pair.second.buckets[i].count;
            }
        }
    }
    for (const auto& pair : other.spans) {
        spans[pair.first] = pair.second;
        next_span_id = std::max(next_span_id, pair.first + 1);
    }
}

void TelemetryRegistry::reset() {
    samples.clear();
    histograms.clear();
    spans.clear();
    next_span_id = 1;
}

std::string TelemetryRegistry::metric_key(const std::string& name,
                                          const std::map<std::string, std::string>& labels) {
    return name + "{" + labels_text(labels) + "}";
}

bool TelemetryParser::parse_text(const std::string& text, TelemetryRegistry& registry) const {
    std::istringstream input(text);
    std::string line;
    bool seen = false;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> parts = split(line, ' ');
        if (parts.size() < 4) continue;
        try {
            if (parts[0] == "counter") {
                registry.add_counter(parts[1], std::stod(parts[2]), parse_labels(parts[3]),
                                     parts.size() > 4 ? std::stoull(parts[4]) : 0);
                seen = true;
            } else if (parts[0] == "gauge") {
                registry.set_gauge(parts[1], std::stod(parts[2]), parse_labels(parts[3]),
                                   parts.size() > 4 ? std::stoull(parts[4]) : 0);
                seen = true;
            } else if (parts[0] == "histogram" && parts.size() >= 5) {
                registry.observe_histogram(parts[1], std::stod(parts[2]), parse_bounds(parts[3]),
                                           std::stoull(parts[4]));
                seen = true;
            } else if (parts[0] == "span" && parts.size() >= 5) {
                uint64_t parent = std::stoull(parts[2]);
                uint64_t start = std::stoull(parts[3]);
                uint64_t end = std::stoull(parts[4]);
                uint64_t id = registry.start_span(parts[1], parent, start);
                registry.finish_span(id, end, parts.size() > 5 ? parse_labels(parts[5]) : std::map<std::string, std::string>());
                seen = true;
            }
        } catch (...) {
            return false;
        }
    }
    return seen;
}

std::string TelemetryParser::emit_report(const TelemetryReport& report) const {
    return report.summary();
}

std::map<std::string, std::string> TelemetryParser::parse_labels(const std::string& text) {
    std::map<std::string, std::string> labels;
    if (text == "-") return labels;
    for (const auto& item : split(text, ',')) {
        size_t eq = item.find('=');
        if (eq == std::string::npos) continue;
        labels[item.substr(0, eq)] = item.substr(eq + 1);
    }
    return labels;
}

std::vector<double> TelemetryParser::parse_bounds(const std::string& text) {
    std::vector<double> bounds;
    for (const auto& item : split(text, ',')) {
        bounds.push_back(std::stod(item));
    }
    if (bounds.empty()) bounds = {1.0, 10.0, 100.0};
    return bounds;
}

} // namespace FenrirDB

