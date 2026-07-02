#include "vectorized_executor_advanced.h"
#include "logger.h"

namespace FenrirDB {

VectorizedHashJoin::VectorizedHashJoin(std::unique_ptr<VectorizedExecutor> left,
                                       std::unique_ptr<VectorizedExecutor> right,
                                       const std::string& l_col, const std::string& r_col)
    : left_child(std::move(left)), right_child(std::move(right)), left_col(l_col), right_col(r_col), built(false) {}

void VectorizedHashJoin::init() {
    left_child->init();
    right_child->init();
    build_table.clear();
    built = false;
    Logger::get_instance().info("Vectorized", "Initialized VectorizedHashJoin");
}

void VectorizedHashJoin::build_hash_table() {
    VectorBatch left_batch;
    while (left_child->next(left_batch)) {
        auto it = left_batch.cols.find(left_col);
        if (it == left_batch.cols.end()) continue;

        const auto& key_col = it->second;
        for (size_t idx : left_batch.selection_vector) {
            if (idx >= key_col.data.size()) continue;
            const auto& cell = key_col.data[idx];
            
            std::string hash_key = "";
            if (cell.type == VariantType::STRING) hash_key = cell.get_string();
            else if (cell.type == VariantType::INT) hash_key = std::to_string(cell.get_int());

            // Build full row representation
            std::unordered_map<std::string, Variant> row;
            for (const auto& pair : left_batch.cols) {
                if (idx < pair.second.data.size()) {
                    row[pair.first] = pair.second.data[idx];
                }
            }
            build_table.insert({hash_key, std::move(row)});
        }
    }
    built = true;
    Logger::get_instance().info("Vectorized", "Hash join build table complete. Size: " + std::to_string(build_table.size()));
}

bool VectorizedHashJoin::next(VectorBatch& batch) {
    if (!built) {
        build_hash_table();
    }

    batch.clear();
    VectorBatch right_batch;

    while (right_child->next(right_batch)) {
        auto it = right_batch.cols.find(right_col);
        if (it == right_batch.cols.end()) continue;

        const auto& key_col = it->second;
        
        // Let's create result vectors
        std::unordered_map<std::string, std::vector<Variant>> result_vectors;
        std::vector<size_t> result_selection;
        size_t result_idx = 0;

        for (size_t idx : right_batch.selection_vector) {
            if (idx >= key_col.data.size()) continue;
            const auto& cell = key_col.data[idx];

            std::string probe_key = "";
            if (cell.type == VariantType::STRING) probe_key = cell.get_string();
            else if (cell.type == VariantType::INT) probe_key = std::to_string(cell.get_int());

            // Materialize matching build-side rows for this probe key.
            const std::unordered_map<std::string, Variant>* left_matches[128];
            size_t match_count = 0;

            auto range = build_table.equal_range(probe_key);
            for (auto match_it = range.first; match_it != range.second; ++match_it) {
                // Copy candidates into the local join workspace.
                left_matches[match_count++] = &(match_it->second);
            }

            for (size_t m = 0; m < match_count; ++m) {
                if (result_idx >= VECTOR_LIMIT) {
                    break;
                }
                const auto& left_row = *left_matches[m];
                
                // Add left columns
                for (const auto& pair : left_row) {
                    result_vectors[pair.first].push_back(pair.second);
                }
                // Add right columns
                for (const auto& pair : right_batch.cols) {
                    if (idx < pair.second.data.size()) {
                        result_vectors[pair.first].push_back(pair.second.data[idx]);
                    } else {
                        result_vectors[pair.first].push_back(Variant());
                    }
                }
                result_selection.push_back(result_idx++);
            }
        }

        if (result_idx > 0) {
            for (const auto& pair : result_vectors) {
                batch.add_column(pair.first, pair.second);
            }
            batch.selection_vector = std::move(result_selection);
            batch.size = result_idx;
            return true;
        }
    }
    return false;
}

void VectorizedHashJoin::close() {
    left_child->close();
    right_child->close();
}

VectorizedAggregation::VectorizedAggregation(std::unique_ptr<VectorizedExecutor> ch,
                                             const std::vector<std::string>& g_cols,
                                             const std::string& a_col)
    : child(std::move(ch)), group_cols(g_cols), agg_col(a_col), cursor(0), computed(false) {}

void VectorizedAggregation::init() {
    child->init();
    agg_map.clear();
    finalized_results.clear();
    cursor = 0;
    computed = false;
    Logger::get_instance().info("Vectorized", "Initialized VectorizedAggregation");
}

void VectorizedAggregation::compute_aggregates() {
    VectorBatch batch;
    while (child->next(batch)) {
        auto agg_it = batch.cols.find(agg_col);
        if (agg_it == batch.cols.end()) continue;

        const auto& val_col = agg_it->second;

        for (size_t idx : batch.selection_vector) {
            std::string group_key = "";
            for (const auto& g_col : group_cols) {
                auto g_it = batch.cols.find(g_col);
                if (g_it != batch.cols.end() && idx < g_it->second.data.size()) {
                    const auto& cell = g_it->second.data[idx];
                    if (cell.type == VariantType::STRING) group_key += cell.get_string() + "#";
                    else if (cell.type == VariantType::INT) group_key += std::to_string(cell.get_int()) + "#";
                }
            }

            if (idx >= val_col.data.size()) continue;
            const auto& cell = val_col.data[idx];
            if (cell.type != VariantType::INT) continue;

            int val = cell.get_int();
            auto it = agg_map.find(group_key);
            if (it == agg_map.end()) {
                agg_map[group_key] = {1, val, val, val};
            } else {
                it->second.count++;
                it->second.sum += val;
                it->second.min = std::min(it->second.min, val);
                it->second.max = std::max(it->second.max, val);
            }
        }
    }

    for (const auto& pair : agg_map) {
        finalized_results.push_back({pair.first, pair.second});
    }
    computed = true;
    Logger::get_instance().info("Vectorized", "Aggregation computation finished. Groups: " + std::to_string(finalized_results.size()));
}

bool VectorizedAggregation::next(VectorBatch& batch) {
    if (!computed) {
        compute_aggregates();
    }

    batch.clear();
    if (cursor >= finalized_results.size()) return false;

    size_t batch_size = std::min(VECTOR_LIMIT, finalized_results.size() - cursor);
    if (batch_size == 0) return false;

    std::vector<Variant> group_keys_col;
    std::vector<Variant> count_col;
    std::vector<Variant> sum_col;
    std::vector<Variant> min_col;
    std::vector<Variant> max_col;

    for (size_t i = 0; i < batch_size; ++i) {
        const auto& item = finalized_results[cursor + i];
        group_keys_col.push_back(Variant(item.first));
        count_col.push_back(Variant(item.second.count));
        sum_col.push_back(Variant(item.second.sum));
        min_col.push_back(Variant(item.second.min));
        max_col.push_back(Variant(item.second.max));
    }

    batch.add_column("group_key", group_keys_col);
    batch.add_column("count", count_col);
    batch.add_column("sum", sum_col);
    batch.add_column("min", min_col);
    batch.add_column("max", max_col);

    batch.selection_vector.resize(batch_size);
    for (size_t i = 0; i < batch_size; ++i) {
        batch.selection_vector[i] = i;
    }
    batch.size = batch_size;

    cursor += batch_size;
    return true;
}

void VectorizedAggregation::close() {
    child->close();
}

} // namespace FenrirDB
