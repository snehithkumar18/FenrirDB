#include "../src/runtime_expression.h"
#include "../src/runtime_manifest.h"
#include "../src/runtime_pipeline.h"
#include "../src/segment_cache.h"
#include "../src/workload_codec.h"

#include <cassert>
#include <iostream>

int main() {
    FenrirDB::RuntimeManifestParser manifest_parser;
    FenrirDB::RuntimeManifest manifest;
    assert(manifest_parser.parse_text(
        "[base]\n"
        "shards=2\n"
        "replicas=2\n"
        "mode=primary\n"
        "[analytics]\n"
        "parent=base\n"
        "shards=3\n",
        manifest));
    assert(manifest.total_shards == 7);
    assert(manifest.effective_properties["analytics.mode"] == "primary");

    FenrirDB::RuntimeRow row;
    row.set("score", FenrirDB::Variant(21));
    row.set("region", FenrirDB::Variant(std::string("west")));
    FenrirDB::RuntimeExpressionEngine engine;
    assert(engine.evaluate_bool("score > 10", row));
    assert(engine.evaluate_bool("contains(region,'we')", row));

    FenrirDB::PipelinePlan plan;
    assert(plan.parse_text(
        "load|users\n"
        "filter|score > 10\n"
        "project|id,score,region\n"
        "window|group|rows|3\n"
        "preview|gray\n"));
    FenrirDB::PipelineExecutor executor;
    FenrirDB::PipelineTable output = executor.execute(plan, manifest);
    assert(!output.rows.empty());

    FenrirDB::SegmentCache cache(3);
    cache.put(1, "users", {1, 2, 3, 4});
    cache.put(2, "users", {5, 6, 7});
    assert(cache.snapshot(1).size() == 4);
    cache.compact_owner("users", 2);
    assert(cache.size() == 2);

    FenrirDB::BinaryWorkload workload;
    FenrirDB::WorkloadOp op;
    op.type = FenrirDB::WorkloadOpType::EXPRESSION;
    op.name = "expr";
    op.payload.assign({'s', 'c', 'o', 'r', 'e', ' ', '>', ' ', '1'});
    workload.ops.push_back(op);
    auto encoded = workload.serialize();
    FenrirDB::BinaryWorkload decoded;
    FenrirDB::BinaryWorkloadReader reader;
    assert(reader.parse(encoded.data(), encoded.size(), decoded));
    assert(decoded.ops.size() == 1);

    std::cout << "runtime subsystem tests passed\n";
    return 0;
}
