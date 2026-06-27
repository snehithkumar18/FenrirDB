#include "query_engine_compiler_pivot.h"
#include "logger.h"
#include <algorithm>
#include <unordered_map>

namespace FenrirDB {

PivotExecutor::PivotExecutor(std::unique_ptr<AbstractExecutor> ch, const PivotSpec& ps)
    : child(std::move(ch)), spec(ps) {}

void PivotExecutor::compute_pivot_table() {
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

    // Map: Group Key String -> pivoted Document
    std::unordered_map<std::string, Document> groups;

    for (const auto& d : all_docs) {
        // Build the composite group key
        std::string group_key = "";
        Document pivoted_doc;
        for (const auto& col : spec.group_cols) {
            Variant val;
            if (d.get_field(col, val)) {
                pivoted_doc.set_field(col, val);
                if (val.type == VariantType::STRING) group_key += val.get_string() + "#";
                else if (val.type == VariantType::INT) group_key += std::to_string(val.get_int()) + "#";
            }
        }

        // Initialize pivoted document fields with 0 defaults
        if (groups.find(group_key) == groups.end()) {
            for (const auto& pv : spec.pivot_values) {
                pivoted_doc.set_field(pv, Variant(0));
            }
            groups[group_key] = pivoted_doc;
        }

        // Extract pivot value
        Variant pivot_v;
        if (d.get_field(spec.pivot_col, pivot_v) && pivot_v.type == VariantType::STRING) {
            std::string pv_str = pivot_v.get_string();
            // Verify if pivot value matches our target specifications
            if (std::find(spec.pivot_values.begin(), spec.pivot_values.end(), pv_str) != spec.pivot_values.end()) {
                Variant val_v;
                if (d.get_field(spec.value_col, val_v) && val_v.type == VariantType::INT) {
                    // Update pivoted field summation
                    Variant existing_v;
                    int cur_sum = 0;
                    if (groups[group_key].get_field(pv_str, existing_v) && existing_v.type == VariantType::INT) {
                        cur_sum = existing_v.get_int();
                    }
                    groups[group_key].set_field(pv_str, Variant(cur_sum + val_v.get_int()));
                }
            }
        }
    }

    // Flatten maps to results table
    for (const auto& pair : groups) {
        materialized_results.push_back(pair.second);
    }
}

void PivotExecutor::init() {
    compute_pivot_table();
    cursor = 0;
    Logger::get_instance().info("Executor", "PivotExecutor computed pivot maps. Total size=" + std::to_string(materialized_results.size()));
}

bool PivotExecutor::next(Document& doc, RecordID& rid) {
    if (cursor < materialized_results.size()) {
        doc = materialized_results[cursor++];
        rid = { 0, 0 };
        return true;
    }
    return false;
}

void PivotExecutor::close() {
    // Materialized child is already closed
}

} // namespace FenrirDB
