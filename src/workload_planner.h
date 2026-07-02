#ifndef FENRIRDB_WORKLOAD_PLANNER_H
#define FENRIRDB_WORKLOAD_PLANNER_H

#include "catalog.h"
#include "query.h"

#include <cstdint>
#include <string>
#include <vector>

namespace FenrirDB {

enum class WorkloadStatementType : uint8_t {
    INSERT = 1,
    FIND = 2,
    UPDATE = 3,
    DELETE_ROWS = 4,
    SCAN = 5,
    AGGREGATE = 6
};

struct WorkloadPredicate {
    std::string field;
    QueryOp op = QueryOp::EQ;
    Variant value;
};

struct WorkloadAssignment {
    std::string field;
    Variant value;
};

struct WorkloadStatement {
    WorkloadStatementType type = WorkloadStatementType::SCAN;
    std::string table;
    std::vector<WorkloadPredicate> predicates;
    std::vector<WorkloadAssignment> assignments;
    Document insert_document;
    std::vector<std::string> projection;
    std::string aggregate_field;
    int limit = 0;
};

enum class PlanStepType : uint8_t {
    VALIDATE_INPUT = 1,
    MATERIALIZE_DEFAULTS = 2,
    INDEX_LOOKUP = 3,
    TABLE_SCAN = 4,
    FILTER = 5,
    PROJECT = 6,
    APPLY_UPDATE = 7,
    DELETE_MARK = 8,
    AGGREGATE = 9
};

struct PlanStep {
    PlanStepType type = PlanStepType::TABLE_SCAN;
    std::string table;
    std::string index;
    std::vector<std::string> fields;
    uint64_t estimated_rows = 0;
    uint64_t estimated_cost = 0;
};

struct WorkloadPlan {
    WorkloadStatement statement;
    std::vector<PlanStep> steps;
    std::vector<ValidationIssue> diagnostics;
    uint64_t total_cost = 0;
    bool accepted = false;

    std::string explain() const;
};

class WorkloadParser {
public:
    bool parse_text(const std::string& text, std::vector<WorkloadStatement>& out) const;

private:
    static bool parse_statement(const std::string& line, WorkloadStatement& out);
    static bool parse_predicate(const std::string& text, WorkloadPredicate& out);
    static bool parse_assignment(const std::string& text, WorkloadAssignment& out);
    static Variant parse_value(const std::string& text);
};

class WorkloadPlanner {
public:
    explicit WorkloadPlanner(const Catalog& catalog);

    WorkloadPlan plan(const WorkloadStatement& statement) const;
    std::vector<WorkloadPlan> plan_all(const std::vector<WorkloadStatement>& statements) const;

private:
    const Catalog& catalog;

    void validate_statement(const TableSchema& schema, WorkloadPlan& plan) const;
    void plan_insert(const TableSchema& schema, WorkloadPlan& plan) const;
    void plan_read_like(const TableSchema& schema, WorkloadPlan& plan) const;
    void plan_update(const TableSchema& schema, WorkloadPlan& plan) const;
    void plan_delete(const TableSchema& schema, WorkloadPlan& plan) const;
    void plan_aggregate(const TableSchema& schema, WorkloadPlan& plan) const;

    const IndexDefinition* choose_index(const TableSchema& schema,
                                        const std::vector<WorkloadPredicate>& predicates) const;
    static uint64_t estimate_base_rows(const TableSchema& schema);
    static uint64_t estimate_selectivity(const WorkloadPredicate& predicate, const IndexDefinition* index);
    static void add_step(WorkloadPlan& plan, const PlanStep& step);
};

} // namespace FenrirDB

#endif // FENRIRDB_WORKLOAD_PLANNER_H
