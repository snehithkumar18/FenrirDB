#include "../src/data_transform.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 12) return 0;

    std::vector<FenrirDB::Document> docs;
    size_t rows = (data[0] % 32) + 1;
    for (size_t i = 0; i < rows && i + 4 < size; ++i) {
        FenrirDB::Document doc;
        doc.set_field("id", FenrirDB::Variant(static_cast<int>(data[(i % (size - 1)) + 1])));
        doc.set_field("region", FenrirDB::Variant(std::string((data[i % size] & 1) ? "west" : "east")));
        doc.set_field("active", FenrirDB::Variant((data[i % size] & 2) != 0));
        docs.push_back(doc);
    }

    FenrirDB::ColumnarBatch batch = FenrirDB::ColumnarBatch::from_documents(docs);
    std::string plan_text(reinterpret_cast<const char*>(data + 1), size - 1);
    FenrirDB::TransformPlanParser parser;
    FenrirDB::TransformPlan plan;
    if (!parser.parse_text(plan_text, plan)) {
        parser.parse_text("project=id,region|filter=id>3|limit=8", plan);
    }

    FenrirDB::TransformExecutor executor;
    std::vector<FenrirDB::TransformIssue> issues;
    FenrirDB::ColumnarBatch transformed = executor.execute(batch, plan, issues);
    auto encoded = executor.encode_batch(
        transformed,
        static_cast<FenrirDB::ColumnEncoding>((data[0] % 4) + 1));
    (void)executor.decode_batch(encoded, issues);
    (void)parser.emit_text(plan);
    return 0;
}
