#include "optimizer_cbo_stats.h"
#include "logger.h"
#include <algorithm>

namespace FenrirDB {

StatsEstimator::StatsEstimator(const TableStatistics* stats) : table_stats(stats) {}

double StatsEstimator::estimate_predicate_selectivity(const QueryNode& node) const {
    if (!table_stats) return 0.5;

    // Evaluate logical AND operators
    if (node.op == QueryOp::AND) {
        double left_sel = 0.5;
        double right_sel = 0.5;
        
        if (node.left_child) {
            left_sel = estimate_predicate_selectivity(*node.left_child);
        }
        if (node.right_child) {
            right_sel = estimate_predicate_selectivity(*node.right_child);
        }

        double sel = left_sel * right_sel;
        Logger::get_instance().info("CBOStats", "AND selectivity estimated: " + std::to_string(sel));
        return sel;
    }

    // Evaluate logical OR operators
    if (node.op == QueryOp::OR) {
        double left_sel = 0.5;
        double right_sel = 0.5;

        if (node.left_child) {
            left_sel = estimate_predicate_selectivity(*node.left_child);
        }
        if (node.right_child) {
            right_sel = estimate_predicate_selectivity(*node.right_child);
        }

        double sel = left_sel + right_sel - (left_sel * right_sel);
        Logger::get_instance().info("CBOStats", "OR selectivity estimated: " + std::to_string(sel));
        return sel;
    }

    // Evaluate comparison operators
    double sel = table_stats->get_selectivity(node.field, node.op, node.value);
    Logger::get_instance().info("CBOStats", "Predicate selectivity on '" + node.field + "' estimated: " + std::to_string(sel));
    return sel;
}

double StatsEstimator::estimate_join_selectivity(const std::string& col1, const std::string& col2) const {
    if (!table_stats) return 0.01;

    double distinct1 = 100.0;
    double distinct2 = 100.0;

    const auto& col_stats = table_stats->get_column_stats();
    auto it1 = col_stats.find(col1);
    if (it1 != col_stats.end()) {
        distinct1 = it1->second.num_distinct;
    }

    auto it2 = col_stats.find(col2);
    if (it2 != col_stats.end()) {
        distinct2 = it2->second.num_distinct;
    }

    double max_distinct = std::max(distinct1, distinct2);
    if (max_distinct <= 0) return 0.01;

    double sel = 1.0 / max_distinct;
    Logger::get_instance().info("CBOStats", "Join selectivity on '" + col1 + "' and '" + col2 + "' estimated: " + std::to_string(sel));
    return sel;
}

} // namespace FenrirDB
