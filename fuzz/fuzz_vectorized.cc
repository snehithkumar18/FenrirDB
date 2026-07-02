#include "../src/vectorized_executor.h"
#include "../src/vectorized_executor_advanced.h"
#include "../src/query.h"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <memory>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 10) return 0;

    // Create documents for left and right children
    std::vector<FenrirDB::Document> left_docs;
    std::vector<FenrirDB::Document> right_docs;

    // Left child: up to 200 documents with the same key "match_key".
    size_t num_left = size % 200;
    for (size_t i = 0; i < num_left; ++i) {
        FenrirDB::Document doc;
        doc.set_field("id", FenrirDB::Variant(static_cast<int>(i)));
        doc.set_field("join_key", FenrirDB::Variant("match_key"));
        left_docs.push_back(doc);
    }

    // Right child: one document with the same key "match_key"
    FenrirDB::Document right_doc;
    right_doc.set_field("id", FenrirDB::Variant(999));
    right_doc.set_field("join_key", FenrirDB::Variant("match_key"));
    right_docs.push_back(right_doc);

    // Create vectorized executors
    auto left_scan = std::make_unique<FenrirDB::VectorizedSeqScan>(left_docs);
    auto right_scan = std::make_unique<FenrirDB::VectorizedSeqScan>(right_docs);

    FenrirDB::VectorizedHashJoin hash_join(std::move(left_scan), std::move(right_scan), "join_key", "join_key");

    hash_join.init();
    FenrirDB::VectorBatch batch;
    // Perform join and consume all produced batches.
    while (hash_join.next(batch)) {
        // Consume batch
    }
    hash_join.close();

    return 0;
}
