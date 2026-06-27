#include "query_engine_compiler_window_ranking.h"
#include "logger.h"
#include <algorithm>
#include <unordered_map>

namespace FenrirDB {

RankingWindowExecutor::RankingWindowExecutor(std::unique_ptr<AbstractExecutor> ch, const RankingSpec& rs)
    : child(std::move(ch)), spec(rs) {}

void RankingWindowExecutor::compute_ranking_windows() {
    materialized_results.clear();
    child->init();

    // 1. Fetch all documents from child
    std::vector<Document> all_docs;
    Document doc;
    RecordID rid;
    while (child->next(doc, rid)) {
        all_docs.push_back(doc);
    }
    child->close();

    // 2. Partition documents
    std::unordered_map<std::string, std::vector<Document>> partitions;
    for (const auto& d : all_docs) {
        std::string part_key = "";
        if (!spec.partition_col.empty()) {
            Variant val;
            if (d.get_field(spec.partition_col, val)) {
                if (val.type == VariantType::STRING) part_key = val.get_string();
                else if (val.type == VariantType::INT) part_key = std::to_string(val.get_int());
            }
        }
        partitions[part_key].push_back(d);
    }

    // 3. Process partitions
    for (auto& pair : partitions) {
        auto& part_docs = pair.second;

        // Sort partition if order_col specified
        if (!spec.order_col.empty()) {
            std::sort(part_docs.begin(), part_docs.end(), [&](const Document& a, const Document& b) {
                Variant val_a, val_b;
                a.get_field(spec.order_col, val_a);
                b.get_field(spec.order_col, val_b);

                if (val_a.type == VariantType::INT && val_b.type == VariantType::INT) {
                    return val_a.get_int() < val_b.get_int();
                } else if (val_a.type == VariantType::STRING && val_b.type == VariantType::STRING) {
                    return val_a.get_string() < val_b.get_string();
                }
                return false;
            });
        }

        // Apply window function
        for (size_t i = 0; i < part_docs.size(); ++i) {
            auto& d = part_docs[i];
            Variant result_val = spec.default_val;

            if (spec.func == RankingFuncType::LAG) {
                int target_idx = static_cast<int>(i) - spec.offset;
                if (target_idx >= 0) {
                    part_docs[target_idx].get_field(spec.target_col, result_val);
                }
            } else if (spec.func == RankingFuncType::LEAD) {
                int target_idx = static_cast<int>(i) + spec.offset;
                if (target_idx < static_cast<int>(part_docs.size())) {
                    part_docs[target_idx].get_field(spec.target_col, result_val);
                }
            } else if (spec.func == RankingFuncType::FIRST_VALUE) {
                if (!part_docs.empty()) {
                    part_docs[0].get_field(spec.target_col, result_val);
                }
            } else if (spec.func == RankingFuncType::LAST_VALUE) {
                if (!part_docs.empty()) {
                    part_docs.back().get_field(spec.target_col, result_val);
                }
            }

            d.set_field("ranking_result", result_val);
            materialized_results.push_back(d);
        }
    }
}

void RankingWindowExecutor::init() {
    compute_ranking_windows();
    cursor = 0;
    Logger::get_instance().info("Executor", "RankingWindowExecutor computed ranking window states.");
}

bool RankingWindowExecutor::next(Document& doc, RecordID& rid) {
    if (cursor < materialized_results.size()) {
        doc = materialized_results[cursor++];
        rid = { 0, 0 };
        return true;
    }
    return false;
}

void RankingWindowExecutor::close() {
    // Materialized child is already closed
}

} // namespace FenrirDB
