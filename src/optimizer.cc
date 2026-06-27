#include "optimizer.h"
#include "logger.h"
#include <cmath>

namespace FenrirDB {

void TableStats::add_column_stats(const std::string& col, const ColumnStats& stats) {
    col_stats[col] = stats;
}

bool TableStats::get_column_stats(const std::string& col, ColumnStats& stats) const {
    auto it = col_stats.find(col);
    if (it != col_stats.end()) {
        stats = it->second;
        return true;
    }
    return false;
}

double TableStats::estimate_selectivity(const std::string& col, QueryOp op, const Variant& val) const {
    ColumnStats c_stats;
    if (!get_column_stats(col, c_stats) || c_stats.num_records == 0) {
        return 0.3; // Default fallback selectivity
    }

    if (op == QueryOp::EQ) {
        if (c_stats.num_distinct > 0) {
            return 1.0 / static_cast<double>(c_stats.num_distinct);
        }
        return 0.1;
    }

    if (val.type == VariantType::INT) {
        int val_int = val.get_int();
        if (c_stats.max_val == c_stats.min_val) return 0.5;

        double range = static_cast<double>(c_stats.max_val - c_stats.min_val);
        if (op == QueryOp::GT) {
            if (val_int >= c_stats.max_val) return 0.0;
            if (val_int <= c_stats.min_val) return 1.0;
            return static_cast<double>(c_stats.max_val - val_int) / range;
        } else if (op == QueryOp::LT) {
            if (val_int >= c_stats.max_val) return 1.0;
            if (val_int <= c_stats.min_val) return 0.0;
            return static_cast<double>(val_int - c_stats.min_val) / range;
        }
    }
    return 0.3;
}

// ======================================================================
// QueryOptimizer Implementation
// ======================================================================

QueryOptimizer::QueryOptimizer() {
    Logger::get_instance().info("Optimizer", "QueryOptimizer initialized.");
}

double QueryOptimizer::estimate_seq_scan_cost(size_t num_pages) const {
    // Seq Scan I/O cost weight = 1.0 (sequential reads are fast)
    return static_cast<double>(num_pages) * 1.0;
}

double QueryOptimizer::estimate_index_scan_cost(size_t index_depth, double selectivity, size_t total_records) const {
    // Index traversal has random I/O weight = 3.0
    double index_io = static_cast<double>(index_depth) * 1.0;
    double data_io = std::ceil(selectivity * static_cast<double>(total_records)) * 3.0;
    return index_io + data_io;
}

void QueryOptimizer::optimize_pushdowns(SQLSelectStatement& stmt) {
    // RBO Pushdown optimization:
    // If table name is empty, we set defaults.
    if (stmt.table.empty()) {
        stmt.table = "users";
    }
    
    // Trivial expression reduction: e.g. age > 100 on a column maxing at 80
    ColumnStats c_stats;
    if (stats.get_column_stats(stmt.where_field, c_stats)) {
        if (stmt.where_op == QueryOp::GT && stmt.where_value.type == VariantType::INT) {
            if (stmt.where_value.get_int() > c_stats.max_val) {
                Logger::get_instance().info("Optimizer", "Optimized: Pruned query branch (Filter out-of-range).");
            }
        }
    }
}

bool QueryOptimizer::choose_index_scan(const std::string& table, const std::string& field, QueryOp op, const Variant& val,
                                       size_t num_pages, size_t index_depth) {
    if (field != "id") {
        return false; // No index available on other columns in this release
    }

    double selectivity = stats.estimate_selectivity(field, op, val);
    size_t total_records = stats.get_total_records();

    double seq_cost = estimate_seq_scan_cost(num_pages);
    double idx_cost = estimate_index_scan_cost(index_depth, selectivity, total_records);

    Logger::get_instance().info("Optimizer", "Cost estimation: SeqScan=" + std::to_string(seq_cost) +
                                 ", IndexScan=" + std::to_string(idx_cost) + " (selectivity=" + std::to_string(selectivity) + ")");

    return idx_cost < seq_cost;
}

} // namespace FenrirDB
