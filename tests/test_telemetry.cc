#include "../src/telemetry.h"

#include <cassert>
#include <iostream>
#include <string>

int main() {
    FenrirDB::TelemetryRegistry registry;
    registry.add_counter("queries_total", 3, {{"table", "users"}}, 100);
    registry.add_counter("queries_total", 2, {{"table", "events"}}, 101);
    registry.set_gauge("cache_pages", 42, {{"pool", "main"}}, 102);
    registry.set_gauge("cache_pages", 50, {{"pool", "main"}}, 103);
    registry.observe_histogram("query_ms", 7.5, {1.0, 10.0, 100.0}, 104);
    registry.observe_histogram("query_ms", 80.0, {1.0, 10.0, 100.0}, 105);

    uint64_t root = registry.start_span("query", 0, 1000);
    uint64_t child = registry.start_span("scan", root, 1010);
    registry.finish_span(child, 1200, {{"table", "users"}});
    registry.finish_span(root, 1210, {{"sql", "select"}});

    FenrirDB::TelemetryReport report = registry.snapshot(100);
    assert(report.counters.size() == 2);
    assert(report.gauges.size() == 1);
    assert(report.histograms.size() == 1);
    assert(report.slow_spans.size() == 2);

    FenrirDB::TelemetryParser parser;
    std::string emitted = parser.emit_report(report);
    assert(emitted.find("queries_total") != std::string::npos);

    FenrirDB::TelemetryRegistry parsed;
    assert(parser.parse_text(
        "counter writes_total 4 table=users 200\n"
        "gauge wal_bytes 128 - 201\n"
        "histogram flush_ms 12 1,10,100 202\n"
        "span checkpoint 0 100 250 reason=periodic\n",
        parsed));
    assert(parsed.snapshot(50).slow_spans.size() == 1);

    registry.merge(parsed);
    assert(registry.snapshot(50).counters.size() == 3);
    registry.reset();
    assert(registry.snapshot(1).counters.empty());

    std::cout << "telemetry tests passed\n";
    return 0;
}
