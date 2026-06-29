#include "../src/storage_compressor_huffman.h"
#include <cstdint>
#include <cstddef>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 10) return 0;

    // Interpret first 8 bytes as bit_length
    size_t bit_length = 0;
    for (int i = 0; i < 8; ++i) {
        bit_length |= (static_cast<size_t>(data[i]) << (i * 8));
    }

    // The rest of the bytes are the serialized tree followed by the bitstream
    std::vector<uint8_t> compressed_bytes(data + 8, data + size);

    FenrirDB::HuffmanCompressor compressor;
    compressor.decompress(compressed_bytes, bit_length);

    return 0;
}
