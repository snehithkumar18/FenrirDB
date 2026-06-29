#ifndef FENRIRDB_LOGICAL_PLAN_H
#define FENRIRDB_LOGICAL_PLAN_H

#include <string>
#include <vector>
#include <memory>

namespace FenrirDB {

enum class LogicalPlanType {
    SCAN,
    FILTER,
    PROJECTION,
    JOIN,
    LIMIT,
    SORT,
    AGGREGATION
};

class LogicalPlanNode {
public:
    LogicalPlanType type;
    std::string description;
    std::vector<std::unique_ptr<LogicalPlanNode>> children;

    LogicalPlanNode(LogicalPlanType t, const std::string& desc)
        : type(t), description(desc) {}
    virtual ~LogicalPlanNode() = default;
};

} // namespace FenrirDB

#endif // FENRIRDB_LOGICAL_PLAN_H
