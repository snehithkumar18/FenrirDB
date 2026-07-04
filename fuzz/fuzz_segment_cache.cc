#include "../src/segment_cache.h"

#include <cstddef>
#include <cstdint>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 8) return 0;

    // Run multiple iterations to trigger temporal bugs (Bugs 10-12)
    for (int iter = 0; iter < 5; iter++) {
        FenrirDB::SegmentCache cache((data[0] % 8) + 1);
        std::vector<uint8_t> program(data + 1, data + size);
        cache.replay(program);

        for (uint32_t i = 0; i < 8; ++i) {
            (void)cache.snapshot(i);
        }
    }
    return 0;
}
