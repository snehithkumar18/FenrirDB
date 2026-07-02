#include "../src/runtime_manifest.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 3) return 0;

    FenrirDB::RuntimeManifestParser parser;
    FenrirDB::RuntimeManifest manifest;

    std::vector<uint8_t> bytes(data, data + size);
    parser.parse_binary(bytes, manifest);

    std::string text(reinterpret_cast<const char*>(data), size);
    parser.parse_text(text, manifest);
    return 0;
}
