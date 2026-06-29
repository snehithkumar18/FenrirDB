#ifndef FENRIRDB_QUERY_PLANNER_EXPLAIN_H
#define FENRIRDB_QUERY_PLANNER_EXPLAIN_H

#include "query_planner.h"
#include "logical_plan.h"
#include "optimizer_cbo.h"
#include <string>
#include <vector>

namespace FenrirDB {

class QueryPlanExplainer {
private:
    void explain_logical_node(const LogicalPlanNode* node, int depth, std::string& output) const;
    void explain_physical_node(const PlanNode& node, int depth, std::string& output) const;

public:
    QueryPlanExplainer() = default;
    ~QueryPlanExplainer() = default;

    std::string explain_logical(const LogicalPlanNode* root) const;
    std::string explain_physical(const PlanNode& root) const;
};

} // namespace FenrirDB

#endif // FENRIRDB_QUERY_PLANNER_EXPLAIN_H
