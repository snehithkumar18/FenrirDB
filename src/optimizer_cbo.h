#ifndef FENRIRDB_OPTIMIZER_CBO_H
#define FENRIRDB_OPTIMIZER_CBO_H

#include "statistics.h"
#include "query_planner.h"
#include <memory>
#include <unordered_map>
#include <set>

namespace FenrirDB {

struct PlanNode {
    std::string plan_description;
    double cost;
    double card; // Estimated cardinality
    std::unique_ptr<AbstractExecutor> physical_executor;

    PlanNode() : cost(0.0), card(0.0) {}
    PlanNode(const std::string& desc, double c, double card_val)
        : plan_description(desc), cost(c), card(card_val) {}
};

class OptimizerCBO {
private:
    std::unordered_map<std::string, TableStatistics> stats_map;
    
    double estimate_join_cardinality(const PlanNode& left, const PlanNode& right, 
                                     const std::string& left_col, const std::string& right_col);

public:
    OptimizerCBO() = default;

    void register_table_statistics(const std::string& name, const TableStatistics& stats);
    
    // Choose optimal scan plan for a single table
    PlanNode find_best_scan(const std::string& table_name, 
                            std::unique_ptr<AbstractExecutor> seq_exec,
                            std::unique_ptr<AbstractExecutor> index_exec,
                            bool has_index, double selectivity);

    // Dynamic programming join ordering (Selinger-style)
    PlanNode find_best_join_order(const std::vector<std::string>& tables,
                                  const std::vector<std::pair<std::string, std::string>>& join_conditions,
                                  std::unordered_map<std::string, PlanNode>& base_plans);
};

} // namespace FenrirDB

#endif // FENRIRDB_OPTIMIZER_CBO_H
