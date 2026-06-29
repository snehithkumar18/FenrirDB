#ifndef FENRIRDB_OPTIMIZER_CBO_RULES_H
#define FENRIRDB_OPTIMIZER_CBO_RULES_H

#include "query_planner.h"
#include "logical_plan.h"
#include <memory>
#include <string>
#include <vector>

namespace FenrirDB {

class OptimizerCBORule {
public:
    virtual ~OptimizerCBORule() = default;
    virtual std::unique_ptr<LogicalPlanNode> apply(std::unique_ptr<LogicalPlanNode> plan) = 0;
};

class RulePredicatePushdown : public OptimizerCBORule {
private:
    std::unique_ptr<LogicalPlanNode> pushdown(std::unique_ptr<LogicalPlanNode> plan, const QueryNode& filter);

public:
    RulePredicatePushdown() = default;
    std::unique_ptr<LogicalPlanNode> apply(std::unique_ptr<LogicalPlanNode> plan) override;
};

class RuleProjectionPushdown : public OptimizerCBORule {
private:
    std::unique_ptr<LogicalPlanNode> prune(std::unique_ptr<LogicalPlanNode> plan, const std::vector<std::string>& active_fields);

public:
    RuleProjectionPushdown() = default;
    std::unique_ptr<LogicalPlanNode> apply(std::unique_ptr<LogicalPlanNode> plan) override;
};

class RuleSimplifyPredicates : public OptimizerCBORule {
private:
    QueryNode fold_constants(const QueryNode& node);

public:
    RuleSimplifyPredicates() = default;
    std::unique_ptr<LogicalPlanNode> apply(std::unique_ptr<LogicalPlanNode> plan) override;
};

class OptimizerRuleScheduler {
private:
    std::vector<std::unique_ptr<OptimizerCBORule>> rules;

public:
    OptimizerRuleScheduler();
    std::unique_ptr<LogicalPlanNode> optimize(std::unique_ptr<LogicalPlanNode> plan);
};

} // namespace FenrirDB

#endif // FENRIRDB_OPTIMIZER_CBO_RULES_H
