#include "../src/telemetry.h"

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 8) return 0;

    std::string input(reinterpret_cast<const char*>(data), size);
    FenrirDB::TelemetryParser parser;
    FenrirDB::TelemetryRegistry registry;
    if (!parser.parse_text(input, registry)) {
        return 0;
    }

    FenrirDB::TelemetryReport report = registry.snapshot((size % 200) + 1);
    (void)parser.emit_report(report);
    FenrirDB::TelemetryRegistry merged;
    merged.merge(registry);
    (void)merged.snapshot(1).summary();
    return 0;
}
