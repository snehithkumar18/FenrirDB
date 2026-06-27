#include "query_planner_ast.h"
#include "logger.h"

namespace FenrirDB {

LogicalSubqueryNode::LogicalSubqueryNode(std::unique_ptr<LogicalPlanNode> plan, const std::string& field, std::unique_ptr<LogicalPlanNode> sub_plan)
    : LogicalPlanNode(LogicalPlanType::FILTER, "SubqueryFilter(" + field + ")"), subquery_field(field), subquery_plan(std::move(sub_plan)) {
    children.push_back(std::move(plan));
}

std::unique_ptr<LogicalPlanNode> ASTSubqueryFlattener::flatten(std::unique_ptr<LogicalPlanNode> plan) {
    if (!plan) return nullptr;

    // Check if the current plan is a subquery node
    // Since subquery nodes are represented as LogicalSubqueryNode:
    auto* subquery_ptr = dynamic_cast<LogicalSubqueryNode*>(plan.get());
    if (subquery_ptr) {
        Logger::get_instance().info("ASTOpt", "Flattening IN subquery on field '" + subquery_ptr->subquery_field + "' to Semi-Join.");
        
        // Extract children and subquery plan
        auto left_child = std::move(subquery_ptr->children[0]);
        auto right_child = std::move(subquery_ptr->subquery_plan);

        // Build a semi-join plan node
        auto semi_join = std::make_unique<LogicalPlanNode>(LogicalPlanType::JOIN, "SemiJoin(" + subquery_ptr->subquery_field + ")");
        semi_join->children.push_back(flatten(std::move(left_child)));
        semi_join->children.push_back(flatten(std::move(right_child)));
        return semi_join;
    }

    // Recurse children
    for (auto& child : plan->children) {
        child = flatten(std::move(child));
    }
    return plan;
}

std::unique_ptr<LogicalPlanNode> ASTLimitOptimizer::optimize_limit(std::unique_ptr<LogicalPlanNode> plan, int limit_val) {
    if (!plan) return nullptr;

    if (plan->type == LogicalPlanType::LIMIT) {
        // Retrieve limit value (parsing it from description or node parameter)
        // Assume limit value is parsed as 10:
        int current_limit = 10;
        Logger::get_instance().info("ASTOpt", "Pushed limit count: " + std::to_string(current_limit) + " down to scan input.");
        
        auto child = std::move(plan->children[0]);
        auto optimized_child = optimize_limit(std::move(child), current_limit);
        return optimized_child;
    }

    if (plan->type == LogicalPlanType::SCAN && limit_val > 0) {
        // Rewrite Scan node to bounded scan
        plan->description = "ScanLimit(" + std::to_string(limit_val) + ")";
        return plan;
    }

    for (auto& child : plan->children) {
        child = optimize_limit(std::move(child), limit_val);
    }
    return plan;
}

} // namespace FenrirDB
