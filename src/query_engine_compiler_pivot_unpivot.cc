#include "query_engine_compiler_pivot_unpivot.h"
#include "logger.h"

namespace FenrirDB {

UnpivotExecutor::UnpivotExecutor(std::unique_ptr<AbstractExecutor> ch, const UnpivotSpec& us)
    : child(std::move(ch)), spec(us) {}

void UnpivotExecutor::compute_unpivot_table() {
    materialized_results.clear();
    child->init();

    // 1. Fetch all documents from the child executor
    std::vector<Document> all_docs;
    Document doc;
    RecordID rid;
    while (child->next(doc, rid)) {
        all_docs.push_back(doc);
    }
    child->close();

    for (const auto& d : all_docs) {
        for (const auto& target_col : spec.target_cols) {
            Variant col_v;
            if (d.get_field(target_col, col_v)) {
                Document unpivoted_doc;
                
                // Copy grouping columns
                for (const auto& gc : spec.group_cols) {
                    Variant gc_v;
                    if (d.get_field(gc, gc_v)) {
                        unpivoted_doc.set_field(gc, gc_v);
                    }
                }

                // Transpose row fields
                unpivoted_doc.set_field(spec.unpivot_name_col, Variant(target_col));
                unpivoted_doc.set_field(spec.unpivot_val_col, col_v);

                materialized_results.push_back(unpivoted_doc);
            }
        }
    }
}

void UnpivotExecutor::init() {
    compute_unpivot_table();
    cursor = 0;
    Logger::get_instance().info("Executor", "UnpivotExecutor computed unpivot tables. Total size=" + std::to_string(materialized_results.size()));
}

bool UnpivotExecutor::next(Document& doc, RecordID& rid) {
    if (cursor < materialized_results.size()) {
        doc = materialized_results[cursor++];
        rid = { 0, 0 };
        return true;
    }
    return false;
}

void UnpivotExecutor::close() {
    // Materialized child is already closed
}

} // namespace FenrirDB
