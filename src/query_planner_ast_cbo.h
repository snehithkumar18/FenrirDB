#ifndef FENRIRDB_QUERY_PLANNER_AST_CBO_H
#define FENRIRDB_QUERY_PLANNER_AST_CBO_H

#include "query_planner.h"
#include "optimizer_cbo.h"
#include <memory>
#include <string>
#include <vector>

namespace FenrirDB {

class ASTCBOPass {
private:
    OptimizerCBO cost_optimizer;

public:
    explicit ASTCBOPass(const OptimizerCBO& optimizer);
    ~ASTCBOPass() = default;

    // Convert Logical plan nodes to physical cost-optimized plans
    PlanNode compile_to_physical(const LogicalPlanNode* logical_node);
};

} // namespace FenrirDB

#endif // FENRIRDB_QUERY_PLANNER_AST_CBO_H
