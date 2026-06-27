#include "query_engine_compiler_window_agg.h"
#include "logger.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>

namespace FenrirDB {

RollingWindowExecutor::RollingWindowExecutor(std::unique_ptr<AbstractExecutor> ch, const RollingSpec& rs)
    : child(std::move(ch)), spec(rs) {}

void RollingWindowExecutor::compute_rolling_windows() {
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
        if (!spec.partition_col.empty()) {
            Variant val;
            if (d.get_field(spec.partition_col, val)) {
                if (val.type == VariantType::STRING) part_key = val.get_string();
                else if (val.type == VariantType::INT) part_key = std::to_string(val.get_int());
            }
        }
        partitions[part_key].push_back(d);
    }

    // 3. Process each partition
    for (auto& pair : partitions) {
        auto& part_docs = pair.second;

        // Sort partition by order column
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

        // Rolling statistical states
        double sum = 0.0;
        double sum_sq_diff = 0.0;
        double mean = 0.0;
        int count = 0;

        for (size_t i = 0; i < part_docs.size(); ++i) {
            auto& d = part_docs[i];

            double val = 0.0;
            if (!spec.value_col.empty()) {
                Variant v;
                if (d.get_field(spec.value_col, v) && v.type == VariantType::INT) {
                    val = static_cast<double>(v.get_int());
                }
            }

            count++;
            sum += val;

            // Welford's algorithm for rolling mean and variance
            double old_mean = mean;
            mean += (val - mean) / count;
            sum_sq_diff += (val - old_mean) * (val - mean);

            double variance = (count > 1) ? (sum_sq_diff / (count - 1)) : 0.0;
            double stddev = std::sqrt(variance);

            if (spec.func == RollingFuncType::CUMULATIVE_SUM) {
                d.set_field("rolling_result", Variant(static_cast<int>(sum)));
            } else if (spec.func == RollingFuncType::VARIANCE) {
                d.set_field("rolling_result", Variant(static_cast<int>(variance)));
            } else if (spec.func == RollingFuncType::STDDEV) {
                d.set_field("rolling_result", Variant(static_cast<int>(stddev)));
            } else if (spec.func == RollingFuncType::PERCENTILE) {
                // Approximate percentile rank within partition
                double rank = static_cast<double>(count) / part_docs.size() * 100.0;
                d.set_field("rolling_result", Variant(static_cast<int>(rank)));
            }

            materialized_results.push_back(d);
        }
    }
}

void RollingWindowExecutor::init() {
    compute_rolling_windows();
    cursor = 0;
    Logger::get_instance().info("Executor", "RollingWindowExecutor computed rolling statistics.");
}

bool RollingWindowExecutor::next(Document& doc, RecordID& rid) {
    if (cursor < materialized_results.size()) {
        doc = materialized_results[cursor++];
        rid = { 0, 0 };
        return true;
    }
    return false;
}

void RollingWindowExecutor::close() {
    // Materialized child is already closed
}

} // namespace FenrirDB
