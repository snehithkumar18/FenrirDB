#ifndef FENRIRDB_QUERY_PLANNER_AST_H
#define FENRIRDB_QUERY_PLANNER_AST_H

#include "query_planner.h"
#include <memory>
#include <string>
#include <vector>

namespace FenrirDB {

class LogicalSubqueryNode : public LogicalPlanNode {
public:
    std::string subquery_field;
    std::unique_ptr<LogicalPlanNode> subquery_plan;

    LogicalSubqueryNode(std::unique_ptr<LogicalPlanNode> plan, const std::string& field, std::unique_ptr<LogicalPlanNode> sub_plan);
    ~LogicalSubqueryNode() = default;
};

class ASTSubqueryFlattener {
public:
    ASTSubqueryFlattener() = default;
    ~ASTSubqueryFlattener() = default;

    // Flatten nested IN/EXISTS subqueries to SEMI-JOINS
    std::unique_ptr<LogicalPlanNode> flatten(std::unique_ptr<LogicalPlanNode> plan);
};

class ASTLimitOptimizer {
public:
    ASTLimitOptimizer() = default;
    ~ASTLimitOptimizer() = default;

    // Push limit counts down to scan operators
    std::unique_ptr<LogicalPlanNode> optimize_limit(std::unique_ptr<LogicalPlanNode> plan, int limit_val = -1);
};

} // namespace FenrirDB

#endif // FENRIRDB_QUERY_PLANNER_AST_H
