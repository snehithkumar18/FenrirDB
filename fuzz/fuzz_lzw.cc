#include "../src/storage_compressor_lzw.h"
#include <cstdint>
#include <cstddef>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 4) return 0;

    // Interpret data as LZW compressed codes (vector of uint16_t)
    size_t num_codes = size / 2;
    std::vector<uint16_t> codes(num_codes);
    for (size_t i = 0; i < num_codes; ++i) {
        codes[i] = data[i * 2] | (data[i * 2 + 1] << 8);
    }

    FenrirDB::LZWCompressor compressor;
    compressor.decompress(codes);

    return 0;
}
