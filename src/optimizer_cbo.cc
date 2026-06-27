#include "optimizer_cbo.h"
#include "logger.h"
#include <algorithm>
#include <limits>
#include <iostream>

namespace FenrirDB {

void OptimizerCBO::register_table_statistics(const std::string& name, const TableStatistics& stats) {
    stats_map.insert({name, stats});
}

double OptimizerCBO::estimate_join_cardinality(const PlanNode& left, const PlanNode& right,
                                               const std::string& left_col, const std::string& right_col) {
    double card = left.card * right.card;
    
    // Find distinct counts in statistics
    double distinct_left = 100.0;
    double distinct_right = 100.0;

    for (const auto& pair : stats_map) {
        const auto& col_stats = pair.second.get_column_stats();
        auto it_l = col_stats.find(left_col);
        if (it_l != col_stats.end()) {
            distinct_left = it_l->second.num_distinct;
        }
        auto it_r = col_stats.find(right_col);
        if (it_r != col_stats.end()) {
            distinct_right = it_r->second.num_distinct;
        }
    }

    double max_distinct = std::max(distinct_left, distinct_right);
    if (max_distinct > 0) {
        card /= max_distinct;
    }
    return card;
}

PlanNode OptimizerCBO::find_best_scan(const std::string& table_name,
                                     std::unique_ptr<AbstractExecutor> seq_exec,
                                     std::unique_ptr<AbstractExecutor> index_exec,
                                     bool has_index, double selectivity) {
    double total_rows = 1000.0;
    auto it = stats_map.find(table_name);
    if (it != stats_map.end()) {
        total_rows = static_cast<double>(it->second.get_total_rows());
    }

    double seq_scan_cost = total_rows * 0.1; // Cost factor of seq read per row
    double index_scan_cost = std::numeric_limits<double>::max();

    if (has_index) {
        // Cost of B+ Tree traverse + selectiveness of leaves
        index_scan_cost = 5.0 + (total_rows * selectivity * 0.2); 
    }

    Logger::get_instance().info("CBO", "Table: " + table_name + " - SeqCost: " + std::to_string(seq_scan_cost) + ", IndexCost: " + std::to_string(index_scan_cost));

    PlanNode best_plan;
    if (index_scan_cost < seq_scan_cost && index_exec) {
        best_plan.plan_description = "IndexScan(" + table_name + ")";
        best_plan.cost = index_scan_cost;
        best_plan.card = total_rows * selectivity;
        best_plan.physical_executor = std::move(index_exec);
    } else {
        best_plan.plan_description = "SeqScan(" + table_name + ")";
        best_plan.cost = seq_scan_cost;
        best_plan.card = total_rows * selectivity;
        best_plan.physical_executor = std::move(seq_exec);
    }
    return best_plan;
}

PlanNode OptimizerCBO::find_best_join_order(const std::vector<std::string>& tables,
                                            const std::vector<std::pair<std::string, std::string>>& join_conditions,
                                            std::unordered_map<std::string, PlanNode>& base_plans) {
    if (tables.empty()) return PlanNode();
    if (tables.size() == 1) {
        return std::move(base_plans[tables[0]]);
    }

    // Dynamic programming table: subset of table indices (as sorted list) -> optimal PlanNode
    // Since n is small (typically <= 5 in toy workloads), we can use vector representation
    // representing subsets
    size_t n = tables.size();
    std::unordered_map<std::string, PlanNode> dp;

    // Initialize DP with base plans
    for (const auto& t : tables) {
        dp[t] = std::move(base_plans[t]);
    }

    // Iterate join sizes
    for (size_t size = 2; size <= n; ++size) {
        // Generate subsets of size 'size'
        // For simplicity of implementation, let's look at combinations
        // If n is 2, the subset is table[0] + table[1]
        std::vector<std::string> subset_keys;
        // In this execution, we'll join base relations iteratively (Left-Deep Trees)
        // Let's find the best join from size-1 DP results and a new base relation
        for (const auto& table : tables) {
            // Find if 'table' is already in the best partial plan
            // For a left-deep join, we join a DP entry of size (size-1) with a base relation
            // Look up the join condition corresponding to table
            for (const auto& cond : join_conditions) {
                std::string left_tbl = "";
                std::string right_tbl = "";
                
                // Parse tables from conditions
                // e.g. cond.first = "users.id", cond.second = "orders.user_id"
                size_t dot_l = cond.first.find('.');
                size_t dot_r = cond.second.find('.');
                if (dot_l != std::string::npos && dot_r != std::string::npos) {
                    left_tbl = cond.first.substr(0, dot_l);
                    right_tbl = cond.second.substr(0, dot_r);
                }

                if (left_tbl.empty()) continue;

                // Check if one table is our candidate base relation, and other is in DP
                std::string dp_key = "";
                std::string base_key = "";
                std::string dp_col = "";
                std::string base_col = "";

                if (left_tbl == table) {
                    base_key = left_tbl;
                    base_col = cond.first.substr(dot_l + 1);
                    dp_key = right_tbl;
                    dp_col = cond.second.substr(dot_r + 1);
                } else if (right_tbl == table) {
                    base_key = right_tbl;
                    base_col = cond.second.substr(dot_r + 1);
                    dp_key = left_tbl;
                    dp_col = cond.first.substr(dot_l + 1);
                }

                if (dp_key.empty() || dp.find(dp_key) == dp.end() || dp.find(base_key) == dp.end()) {
                    continue;
                }

                // We evaluate Hash Join vs Nested Loop Join for (dp[dp_key] JOIN dp[base_key])
                const auto& left_plan = dp[dp_key];
                const auto& right_plan = dp[base_key];

                double hash_join_cost = left_plan.cost + right_plan.cost + (left_plan.card * 0.05) + (right_plan.card * 0.1);
                double nlj_cost = left_plan.cost + (left_plan.card * right_plan.cost * 0.5);

                double best_join_cost = std::min(hash_join_cost, nlj_cost);
                double join_card = estimate_join_cardinality(left_plan, right_plan, dp_col, base_col);

                std::string new_key = dp_key + "_" + base_key;
                
                PlanNode join_plan;
                join_plan.plan_description = (hash_join_cost < nlj_cost ? "HashJoin(" : "NestedLoopJoin(") + dp_key + ", " + base_key + ")";
                join_plan.cost = best_join_cost;
                join_plan.card = join_card;

                Logger::get_instance().info("CBO", "Sub-Plan: " + join_plan.plan_description + " - Cost: " + std::to_string(best_join_cost) + ", Card: " + std::to_string(join_card));

                // If not exists or cheaper, save to DP
                auto dp_it = dp.find(new_key);
                if (dp_it == dp.end() || dp_it->second.cost > best_join_cost) {
                    dp[new_key] = std::move(join_plan);
                }
            }
        }
    }

    // Return the plan containing all joined tables
    std::string final_key = "";
    for (size_t i = 0; i < tables.size(); ++i) {
        final_key += tables[i] + (i < tables.size() - 1 ? "_" : "");
    }

    auto final_it = dp.find(final_key);
    if (final_it != dp.end()) {
        return std::move(final_it->second);
    }

    // Fallback: return the first base plan
    return std::move(base_plans[tables[0]]);
}

} // namespace FenrirDB
