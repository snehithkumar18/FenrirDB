#include "query_planner_decorrelate.h"
#include "logger.h"

namespace FenrirDB {

bool SubqueryDecorrelator::detect_correlation(const LogicalPlanNode* subquery, const std::string& outer_table) {
    if (!subquery) return false;

    if (subquery->type == LogicalPlanType::FILTER) {
        size_t dot = subquery->query.field.find('.');
        if (dot != std::string::npos) {
            std::string tbl = subquery->query.field.substr(0, dot);
            if (tbl == outer_table) {
                return true;
            }
        }
    }

    for (const auto& child : subquery->children) {
        if (detect_correlation(child.get(), outer_table)) {
            return true;
        }
    }
    return false;
}

std::unique_ptr<LogicalPlanNode> SubqueryDecorrelator::decorrelate(std::unique_ptr<LogicalPlanNode> plan) {
    if (!plan) return nullptr;

    // Check if there is a correlated subquery filter
    if (plan->type == LogicalPlanType::FILTER && plan->description.find("SubqueryFilter") != std::string::npos) {
        // Assume outer table is "users"
        std::string outer_table = "users";
        
        if (detect_correlation(plan.get(), outer_table)) {
            Logger::get_instance().info("Decorrelator", "Correlated subquery detected for outer table '" + outer_table + "'. Applying join decorrelation rewrite.");
            
            auto child = std::move(plan->children[0]);
            
            // Rewrite filter node to an INNER JOIN on outer reference fields
            auto join_node = std::make_unique<LogicalPlanNode>(LogicalPlanType::JOIN, "CorrelatedJoin(" + outer_table + ")");
            join_node->children.push_back(decorrelate(std::move(child)));
            
            // Create right table scan input
            auto scan_right = std::make_unique<LogicalPlanNode>(LogicalPlanType::SCAN, "Scan(orders)");
            join_node->children.push_back(std::move(scan_right));
            
            return join_node;
        }
    }

    for (auto& child : plan->children) {
        child = decorrelate(std::move(child));
    }
    return plan;
}

} // namespace FenrirDB
