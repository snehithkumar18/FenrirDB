#include "../src/runtime_manifest.h"
#include "../src/runtime_pipeline.h"
#include "../src/segment_cache.h"
#include "../src/workload_codec.h"

#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 12) return 0;

    FenrirDB::BinaryWorkload workload;
    FenrirDB::BinaryWorkloadReader reader;
    if (!reader.parse(data, size, workload)) {
        return 0;
    }

    FenrirDB::RuntimeManifestParser manifest_parser;
    FenrirDB::RuntimeManifest manifest;
    manifest_parser.parse_text("[default]\nshards=2\nreplicas=1\nmode=seed\n", manifest);

    FenrirDB::RuntimeExpressionEngine expression_engine;
    FenrirDB::RuntimeRow row;
    row.set("id", FenrirDB::Variant(7));
    row.set("score", FenrirDB::Variant(49));
    row.set("region", FenrirDB::Variant(std::string("west")));
    row.set("deep_alias", FenrirDB::Variant(42));

    FenrirDB::PipelineExecutor pipeline;
    FenrirDB::SegmentCache cache(4);

    for (const auto& op : workload.ops) {
        if (op.type == FenrirDB::WorkloadOpType::MANIFEST) {
            if (op.flags & 1) manifest_parser.parse_binary(op.payload, manifest);
            else manifest_parser.parse_text(std::string(op.payload.begin(), op.payload.end()), manifest);
        } else if (op.type == FenrirDB::WorkloadOpType::EXPRESSION) {
            std::string text(op.payload.begin(), op.payload.end());
            FenrirDB::RuntimeExpressionParser parser;
            auto expr = parser.parse(text);
            if (expr) (void)expression_engine.evaluate(*expr, row);
        } else if (op.type == FenrirDB::WorkloadOpType::PIPELINE) {
            FenrirDB::PipelinePlan plan;
            if (op.flags & 1) plan.parse_binary(op.payload);
            else plan.parse_text(std::string(op.payload.begin(), op.payload.end()));
            if (!plan.stages.empty()) {
                FenrirDB::PipelineTable result = pipeline.execute(plan, manifest);
                (void)pipeline.render_preview(result, op.name == "rgb" ? "rgb" : "gray");
            }
        } else if (op.type == FenrirDB::WorkloadOpType::CACHE) {
            cache.replay(op.payload);
        } else if (op.type == FenrirDB::WorkloadOpType::SERIALIZE) {
            (void)workload.serialize();
        }
    }
    return 0;
}
