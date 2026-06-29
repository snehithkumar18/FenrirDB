#include "../src/optimizer_cbo_stats_histogram.h"
#include <cstdint>
#include <cstddef>
#include <cstring>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 8) return 0;

    // Interpret fuzzer input as a series of double values
    size_t num_doubles = size / 8;
    
    // Setup histogram with range [0.0, 100.0] and 10 buckets
    FenrirDB::EquiWidthHistogram histogram(0.0, 100.0, 10);

    for (size_t i = 0; i < num_doubles; ++i) {
        double val;
        std::memcpy(&val, data + i * 8, 8);
        histogram.add_value(val);
    }

    return 0;
}
