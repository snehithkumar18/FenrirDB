#include "../src/data_transform.h"

#include <cassert>
#include <iostream>
#include <string>

int main() {
    std::vector<FenrirDB::Document> docs;
    for (int i = 0; i < 10; ++i) {
        FenrirDB::Document doc;
        doc.set_field("id", FenrirDB::Variant(i));
        doc.set_field("region", FenrirDB::Variant(std::string(i % 2 == 0 ? "west" : "east")));
        doc.set_field("active", FenrirDB::Variant(i % 3 == 0));
        docs.push_back(doc);
    }

    FenrirDB::ColumnarBatch batch = FenrirDB::ColumnarBatch::from_documents(docs);
    assert(batch.row_count() == 10);
    assert(batch.find_column("id") != nullptr);

    FenrirDB::TransformPlanParser parser;
    FenrirDB::TransformPlan plan;
    assert(parser.parse_text("project=id,region|filter=id>3|limit=3", plan));

    FenrirDB::TransformExecutor executor;
    std::vector<FenrirDB::TransformIssue> issues;
    FenrirDB::ColumnarBatch filtered = executor.execute(batch, plan, issues);
    assert(issues.empty());
    assert(filtered.row_count() == 3);

    auto encoded = executor.encode_batch(filtered, FenrirDB::ColumnEncoding::DICTIONARY);
    assert(!encoded.empty());
    FenrirDB::ColumnarBatch decoded = executor.decode_batch(encoded, issues);
    assert(issues.empty());
    assert(decoded.row_count() == filtered.row_count());

    const FenrirDB::ColumnBlock* id_col = batch.find_column("id");
    FenrirDB::ColumnEncoder encoder;
    FenrirDB::EncodedColumn delta = encoder.encode(*id_col, FenrirDB::ColumnEncoding::DELTA_INT);
    FenrirDB::ColumnBlock roundtrip;
    assert(encoder.decode(delta, roundtrip));
    assert(roundtrip.values.size() == id_col->values.size());
    assert(roundtrip.values[5].get_int() == 5);

    std::cout << "data transform tests passed\n";
    return 0;
}
