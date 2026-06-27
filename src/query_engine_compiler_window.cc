#include "query_engine_compiler_window.h"
#include "logger.h"
#include <algorithm>
#include <map>

namespace FenrirDB {

WindowExecutor::WindowExecutor(std::unique_ptr<AbstractExecutor> ch, const WindowSpec& ws)
    : child(std::move(ch)), spec(ws) {}

void WindowExecutor::compute_window_functions() {
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

    // 2. Partition documents
    std::unordered_map<std::string, std::vector<Document>> partitions;
    for (const auto& d : all_docs) {
        std::string part_key = "";
        if (!spec.partition_by_col.empty()) {
            Variant val;
            if (d.get_field(spec.partition_by_col, val)) {
                if (val.type == VariantType::STRING) part_key = val.get_string();
                else if (val.type == VariantType::INT) part_key = std::to_string(val.get_int());
            }
        }
        partitions[part_key].push_back(d);
    }

    // 3. Process each partition
    for (auto& pair : partitions) {
        auto& part_docs = pair.second;

        // Sort documents inside the partition if order_by_col is specified
        if (!spec.order_by_col.empty()) {
            std::sort(part_docs.begin(), part_docs.end(), [&](const Document& a, const Document& b) {
                Variant val_a, val_b;
                a.get_field(spec.order_by_col, val_a);
                b.get_field(spec.order_by_col, val_b);

                if (val_a.type == VariantType::INT && val_b.type == VariantType::INT) {
                    return val_a.get_int() < val_b.get_int();
                } else if (val_a.type == VariantType::STRING && val_b.type == VariantType::STRING) {
                    return val_a.get_string() < val_b.get_string();
                }
                return false;
            });
        }

        // Apply window function
        int row_num = 1;
        int rank = 1;
        int dense_rank = 1;
        int running_sum = 0;
        int count = 0;
        Variant last_order_val;

        for (size_t i = 0; i < part_docs.size(); ++i) {
            auto& d = part_docs[i];
            
            // Handle ranking ties
            if (i > 0 && !spec.order_by_col.empty()) {
                Variant curr_order_val;
                d.get_field(spec.order_by_col, curr_order_val);
                if (curr_order_val != last_order_val) {
                    rank = static_cast<int>(i + 1);
                    dense_rank++;
                }
                last_order_val = curr_order_val;
            } else if (i == 0 && !spec.order_by_col.empty()) {
                d.get_field(spec.order_by_col, last_order_val);
            }

            // Running sum/avg input extraction
            int target_val = 0;
            if (!spec.target_col.empty()) {
                Variant t_val;
                if (d.get_field(spec.target_col, t_val) && t_val.type == VariantType::INT) {
                    target_val = t_val.get_int();
                }
            }

            running_sum += target_val;
            count++;

            // Set window result field
            if (spec.func == WindowFuncType::ROW_NUMBER) {
                d.set_field("window_result", Variant(row_num++));
            } else if (spec.func == WindowFuncType::RANK) {
                d.set_field("window_result", Variant(rank));
            } else if (spec.func == WindowFuncType::DENSE_RANK) {
                d.set_field("window_result", Variant(dense_rank));
            } else if (spec.func == WindowFuncType::SUM) {
                d.set_field("window_result", Variant(running_sum));
            } else if (spec.func == WindowFuncType::AVG) {
                d.set_field("window_result", Variant(running_sum / count));
            }

            materialized_results.push_back(d);
        }
    }
}

void WindowExecutor::init() {
    compute_window_functions();
    cursor = 0;
    Logger::get_instance().info("Executor", "WindowExecutor computed window results. Total size=" + std::to_string(materialized_results.size()));
}

bool WindowExecutor::next(Document& doc, RecordID& rid) {
    if (cursor < materialized_results.size()) {
        doc = materialized_results[cursor++];
        rid = { 0, 0 };
        return true;
    }
    return false;
}

void WindowExecutor::close() {
    // Materialized child is already closed
}

} // namespace FenrirDB
