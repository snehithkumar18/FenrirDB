#include "optimizer_cbo_rules.h"
#include "logger.h"

namespace FenrirDB {

std::unique_ptr<LogicalPlanNode> RulePredicatePushdown::pushdown(std::unique_ptr<LogicalPlanNode> plan, const QueryNode& filter) {
    if (!plan) return nullptr;

    if (plan->type == LogicalPlanType::JOIN) {
        // Check if filter matches left or right child schema
        // Since we check table boundaries based on field name prefixes (e.g. "users.id"):
        size_t dot = filter.field.find('.');
        std::string target_table = "";
        if (dot != std::string::npos) {
            target_table = filter.field.substr(0, dot);
        }

        auto& left = plan->children[0];
        auto& right = plan->children[1];

        // Match table names (assuming scan node description holds table name)
        if (!target_table.empty() && left->description.find(target_table) != std::string::npos) {
            // Push left
            Logger::get_instance().info("Optimizer", "Pushing predicate: " + filter.field + " down to left join input.");
            auto new_filter = std::make_unique<LogicalPlanNode>(LogicalPlanType::FILTER, "Filter(" + filter.field + ")");
            new_filter->query = filter;
            new_filter->children.push_back(std::move(left));
            plan->children[0] = std::move(new_filter);
            return plan;
        } else if (!target_table.empty() && right->description.find(target_table) != std::string::npos) {
            // Push right
            Logger::get_instance().info("Optimizer", "Pushing predicate: " + filter.field + " down to right join input.");
            auto new_filter = std::make_unique<LogicalPlanNode>(LogicalPlanType::FILTER, "Filter(" + filter.field + ")");
            new_filter->query = filter;
            new_filter->children.push_back(std::move(right));
            plan->children[1] = std::move(new_filter);
            return plan;
        }
    }

    // Recurse children
    for (auto& child : plan->children) {
        child = pushdown(std::move(child), filter);
    }
    return plan;
}

std::unique_ptr<LogicalPlanNode> RulePredicatePushdown::apply(std::unique_ptr<LogicalPlanNode> plan) {
    if (!plan) return nullptr;

    if (plan->type == LogicalPlanType::FILTER) {
        // Extract filter query, apply pushdown below
        QueryNode filter = plan->query;
        auto child = std::move(plan->children[0]);
        auto optimized_child = pushdown(std::move(child), filter);
        return optimized_child;
    }

    for (auto& child : plan->children) {
        child = apply(std::move(child));
    }
    return plan;
}

std::unique_ptr<LogicalPlanNode> RuleProjectionPushdown::prune(std::unique_ptr<LogicalPlanNode> plan, const std::vector<std::string>& active_fields) {
    if (!plan) return nullptr;

    if (plan->type == LogicalPlanType::SCAN) {
        // Insert a projection right above the table scan
        Logger::get_instance().info("Optimizer", "Injecting projection prune above scan: " + plan->description);
        std::string proj_desc = "ProjectionPrune(";
        for (const auto& f : active_fields) proj_desc += f + " ";
        proj_desc += ")";
        
        auto proj = std::make_unique<LogicalPlanNode>(LogicalPlanType::PROJECTION, proj_desc);
        proj->children.push_back(std::move(plan));
        return proj;
    }

    for (auto& child : plan->children) {
        child = prune(std::move(child), active_fields);
    }
    return plan;
}

std::unique_ptr<LogicalPlanNode> RuleProjectionPushdown::apply(std::unique_ptr<LogicalPlanNode> plan) {
    if (!plan) return nullptr;

    if (plan->type == LogicalPlanType::PROJECTION) {
        // Collect projection fields
        std::vector<std::string> active_fields;
        // Parse fields from projection description
        // Assuming base columns are gathered:
        active_fields.push_back("id"); // default required fields
        
        auto child = std::move(plan->children[0]);
        auto optimized_child = prune(std::move(child), active_fields);
        plan->children.push_back(std::move(optimized_child));
        return plan;
    }

    for (auto& child : plan->children) {
        child = apply(std::move(child));
    }
    return plan;
}

QueryNode RuleSimplifyPredicates::fold_constants(const QueryNode& node) {
    // If the expression has left and right constant integers, simplify
    QueryNode simplified = node;
    // For example, if we evaluate constant folding optimizations on comparison nodes:
    if (simplified.value.type == VariantType::INT) {
        // Simulating folding pass
    }
    return simplified;
}

std::unique_ptr<LogicalPlanNode> RuleSimplifyPredicates::apply(std::unique_ptr<LogicalPlanNode> plan) {
    if (!plan) return nullptr;

    if (plan->type == LogicalPlanType::FILTER) {
        plan->query = fold_constants(plan->query);
    }

    for (auto& child : plan->children) {
        child = apply(std::move(child));
    }
    return plan;
}

OptimizerRuleScheduler::OptimizerRuleScheduler() {
    rules.push_back(std::make_unique<RuleSimplifyPredicates>());
    rules.push_back(std::make_unique<RulePredicatePushdown>());
    rules.push_back(std::make_unique<RuleProjectionPushdown>());
}

std::unique_ptr<LogicalPlanNode> OptimizerRuleScheduler::optimize(std::unique_ptr<LogicalPlanNode> plan) {
    std::unique_ptr<LogicalPlanNode> optimized_plan = std::move(plan);
    for (const auto& rule : rules) {
        optimized_plan = rule->apply(std::move(optimized_plan));
    }
    return optimized_plan;
}

} // namespace FenrirDB
