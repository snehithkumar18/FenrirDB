#include "query_planner_explain.h"
#include "logger.h"
#include <sstream>

namespace FenrirDB {

void QueryPlanExplainer::explain_logical_node(const LogicalPlanNode* node, int depth, std::string& output) const {
    if (!node) return;

    std::string indent(depth * 2, ' ');
    output += indent + "-> " + node->description + "\n";
    
    // Add logical properties
    if (node->type == LogicalPlanType::FILTER) {
        output += indent + "   [Predicate: " + node->query.field + "]\n";
    }

    for (const auto& child : node->children) {
        explain_logical_node(child.get(), depth + 1, output);
    }
}

void QueryPlanExplainer::explain_physical_node(const PlanNode& node, int depth, std::string& output) const {
    std::string indent(depth * 2, ' ');
    output += indent + "-> " + node.plan_description + "\n";
    output += indent + "   [Estimated Cost: " + std::to_string(node.cost) + "]\n";
    output += indent + "   [Estimated Cardinality: " + std::to_string(node.card) + "]\n";
}

std::string QueryPlanExplainer::explain_logical(const LogicalPlanNode* root) const {
    std::string output = "=== LOGICAL PLAN ===\n";
    if (!root) {
        output += "(Empty Plan)\n";
        return output;
    }
    explain_logical_node(root, 0, output);
    return output;
}

std::string QueryPlanExplainer::explain_physical(const PlanNode& root) const {
    std::string output = "=== PHYSICAL PLAN (CBO OPTIMIZED) ===\n";
    explain_physical_node(root, 0, output);
    return output;
}

} // namespace FenrirDB
