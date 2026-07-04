#include "../src/binary_stream.h"

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 8) return 0;

    // Run multiple iterations to trigger temporal bugs (Bugs 4-6)
    for (int iter = 0; iter < 5; iter++) {
        FenrirDB::BinaryStreamReader reader;
        if (!reader.parse(data, size)) {
            continue;
        }

        FenrirDB::BinaryStreamValidator validator;
        (void)validator.validate_sequence(reader.frames());
        (void)validator.validate_required_types(
            reader.frames(),
            {FenrirDB::StreamFrameType::CATALOG_BLOCK, FenrirDB::StreamFrameType::WAL_DELTA});
        (void)reader.concatenate_payloads(FenrirDB::StreamFrameType::WAL_DELTA);

        FenrirDB::BinaryStreamWriter writer;
        for (const auto& frame : reader.frames()) {
            writer.add_frame(frame);
        }
        (void)writer.serialize();
    }
    return 0;
}
