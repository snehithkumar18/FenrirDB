#include "../src/runtime_manifest.h"
#include "../src/runtime_pipeline.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 6) return 0;

    size_t split = size / 2;
    std::vector<uint8_t> manifest_bytes(data, data + split);
    std::vector<uint8_t> plan_bytes(data + split, data + size);

    FenrirDB::RuntimeManifestParser manifest_parser;
    FenrirDB::RuntimeManifest manifest;
    manifest_parser.parse_binary(manifest_bytes, manifest);
    if (manifest.sections.empty()) {
        manifest_parser.parse_text("[default]\nshards=2\nreplicas=1\n", manifest);
    }

    FenrirDB::PipelinePlan plan;
    if (!plan.parse_binary(plan_bytes)) {
        std::string text(reinterpret_cast<const char*>(data), size);
        plan.parse_text(text);
    }

    FenrirDB::PipelineExecutor executor;
    if (!plan.stages.empty()) {
        FenrirDB::PipelineTable result = executor.execute(plan, manifest);
        (void)executor.render_preview(result, (size & 1) ? "gray" : "rgb");
    }
    return 0;
}
