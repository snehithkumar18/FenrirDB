#include "vectorized_executor.h"
#include "logger.h"
#include <algorithm>

namespace FenrirDB {

void VectorBatch::clear() {
    cols.clear();
    selection_vector.clear();
    size = 0;
}

void VectorBatch::add_column(const std::string& name, const std::vector<Variant>& data) {
    VectorColumn col;
    col.data = data;
    col.has_nulls = false;
    for (const auto& val : data) {
        if (val.type == VariantType::NIL) {
            col.has_nulls = true;
            break;
        }
    }
    cols[name] = std::move(col);
    size = std::max(size, data.size());
}

VectorizedSeqScan::VectorizedSeqScan(const std::vector<Document>& docs)
    : source_docs(docs), cursor(0) {}

void VectorizedSeqScan::init() {
    cursor = 0;
    Logger::get_instance().info("Vectorized", "Initialized VectorizedSeqScan with total rows: " + std::to_string(source_docs.size()));
}

bool VectorizedSeqScan::next(VectorBatch& batch) {
    batch.clear();
    if (cursor >= source_docs.size()) return false;

    size_t batch_size = std::min(VECTOR_LIMIT, source_docs.size() - cursor);
    if (batch_size == 0) return false;

    // Discover column names for this batch
    std::vector<std::string> col_names;
    for (size_t i = 0; i < batch_size; ++i) {
        for (const auto& pair : source_docs[cursor + i].get_fields()) {
            if (std::find(col_names.begin(), col_names.end(), pair.first) == col_names.end()) {
                col_names.push_back(pair.first);
            }
        }
    }

    // Build columnar values
    for (const auto& col_name : col_names) {
        std::vector<Variant> col_data(batch_size);
        for (size_t i = 0; i < batch_size; ++i) {
            Variant val;
            if (source_docs[cursor + i].get_field(col_name, val)) {
                col_data[i] = val;
            } else {
                col_data[i] = Variant(); // Nil default
            }
        }
        batch.add_column(col_name, col_data);
    }

    // Initialize selection vector sequentially
    batch.selection_vector.resize(batch_size);
    for (size_t i = 0; i < batch_size; ++i) {
        batch.selection_vector[i] = i;
    }
    batch.size = batch_size;

    cursor += batch_size;
    return true;
}

void VectorizedSeqScan::close() {
    cursor = source_docs.size();
}

VectorizedFilter::VectorizedFilter(std::unique_ptr<VectorizedExecutor> ch, const std::string& col, QueryOp op, const Variant& val)
    : child(std::move(ch)), filter_col(col), filter_op(op), filter_val(val) {}

void VectorizedFilter::init() {
    child->init();
    Logger::get_instance().info("Vectorized", "Initialized VectorizedFilter on column: " + filter_col);
}

bool VectorizedFilter::next(VectorBatch& batch) {
    while (child->next(batch)) {
        auto it = batch.cols.find(filter_col);
        if (it == batch.cols.end()) {
            // Column missing, prune all rows in batch
            batch.selection_vector.clear();
            continue;
        }

        const auto& col = it->second;
        std::vector<size_t> filtered_selection;

        for (size_t idx : batch.selection_vector) {
            if (idx >= col.data.size()) continue;
            const auto& cell = col.data[idx];

            bool match = false;
            if (cell.type == VariantType::INT && filter_val.type == VariantType::INT) {
                int cell_i = cell.get_int();
                int filter_i = filter_val.get_int();
                if (filter_op == QueryOp::EQ) match = (cell_i == filter_i);
                else if (filter_op == QueryOp::GT) match = (cell_i > filter_i);
                else if (filter_op == QueryOp::LT) match = (cell_i < filter_i);
            } else if (cell.type == VariantType::STRING && filter_val.type == VariantType::STRING) {
                std::string cell_s = cell.get_string();
                std::string filter_s = filter_val.get_string();
                if (filter_op == QueryOp::EQ) match = (cell_s == filter_s);
            }

            if (match) {
                filtered_selection.push_back(idx);
            }
        }

        batch.selection_vector = std::move(filtered_selection);
        if (!batch.selection_vector.empty()) {
            return true;
        }
    }
    return false;
}

void VectorizedFilter::close() {
    child->close();
}

VectorizedProjection::VectorizedProjection(std::unique_ptr<VectorizedExecutor> ch, const std::vector<std::string>& cols)
    : child(std::move(ch)), projection_cols(cols) {}

void VectorizedProjection::init() {
    child->init();
    Logger::get_instance().info("Vectorized", "Initialized VectorizedProjection");
}

bool VectorizedProjection::next(VectorBatch& batch) {
    if (child->next(batch)) {
        // Discard columns not in projection
        std::unordered_map<std::string, VectorColumn> projected_cols;
        for (const auto& col_name : projection_cols) {
            auto it = batch.cols.find(col_name);
            if (it != batch.cols.end()) {
                projected_cols[col_name] = std::move(it->second);
            }
        }
        batch.cols = std::move(projected_cols);
        return true;
    }
    return false;
}

void VectorizedProjection::close() {
    child->close();
}

} // namespace FenrirDB
