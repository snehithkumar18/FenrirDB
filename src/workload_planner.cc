#include "workload_planner.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace FenrirDB {

namespace {

std::string trim(const std::string& input) {
    size_t begin = 0;
    while (begin < input.size() && std::isspace(static_cast<unsigned char>(input[begin]))) ++begin;
    size_t end = input.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1]))) --end;
    return input.substr(begin, end - begin);
}

std::vector<std::string> split(const std::string& text, char delim) {
    std::vector<std::string> out;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, delim)) {
        item = trim(item);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

std::string step_name(PlanStepType type) {
    switch (type) {
        case PlanStepType::VALIDATE_INPUT: return "validate";
        case PlanStepType::MATERIALIZE_DEFAULTS: return "defaults";
        case PlanStepType::INDEX_LOOKUP: return "index_lookup";
        case PlanStepType::FILTER: return "filter";
        case PlanStepType::PROJECT: return "project";
        case PlanStepType::APPLY_UPDATE: return "update";
        case PlanStepType::DELETE_MARK: return "delete";
        case PlanStepType::AGGREGATE: return "aggregate";
        default: return "table_scan";
    }
}

std::string variant_text(const Variant& value) {
    if (value.type == VariantType::INT) return std::to_string(value.get_int());
    if (value.type == VariantType::BOOL) return value.get_bool() ? "true" : "false";
    if (value.type == VariantType::STRING) return "'" + value.get_string() + "'";
    return "nil";
}

} // namespace

std::string WorkloadPlan::explain() const {
    std::ostringstream out;
    out << (accepted ? "accepted" : "rejected") << " cost=" << total_cost << "\n";
    for (const auto& issue : diagnostics) {
        out << "diagnostic " << issue.field << ": " << issue.message << "\n";
    }
    for (const auto& step : steps) {
        out << step_name(step.type) << " table=" << step.table;
        if (!step.index.empty()) out << " index=" << step.index;
        if (!step.fields.empty()) {
            out << " fields=";
            for (size_t i = 0; i < step.fields.size(); ++i) {
                if (i) out << ",";
                out << step.fields[i];
            }
        }
        out << " rows=" << step.estimated_rows << " cost=" << step.estimated_cost << "\n";
    }
    return out.str();
}

bool WorkloadParser::parse_text(const std::string& text, std::vector<WorkloadStatement>& out) const {
    out.clear();
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        WorkloadStatement statement;
        if (parse_statement(line, statement)) {
            out.push_back(statement);
        }
    }
    return !out.empty();
}

bool WorkloadParser::parse_statement(const std::string& line, WorkloadStatement& out) {
    std::vector<std::string> parts = split(line, '|');
    if (parts.size() < 2) return false;
    std::string verb = parts[0];
    out.table = parts[1];
    if (verb == "insert") out.type = WorkloadStatementType::INSERT;
    else if (verb == "find") out.type = WorkloadStatementType::FIND;
    else if (verb == "update") out.type = WorkloadStatementType::UPDATE;
    else if (verb == "delete") out.type = WorkloadStatementType::DELETE_ROWS;
    else if (verb == "scan") out.type = WorkloadStatementType::SCAN;
    else if (verb == "aggregate") out.type = WorkloadStatementType::AGGREGATE;
    else return false;

    for (size_t i = 2; i < parts.size(); ++i) {
        const std::string& part = parts[i];
        if (part.find("where=") == 0) {
            for (const auto& pred_text : split(part.substr(6), ',')) {
                WorkloadPredicate predicate;
                if (parse_predicate(pred_text, predicate)) {
                    out.predicates.push_back(predicate);
                }
            }
        } else if (part.find("set=") == 0) {
            for (const auto& assign_text : split(part.substr(4), ',')) {
                WorkloadAssignment assignment;
                if (parse_assignment(assign_text, assignment)) {
                    out.assignments.push_back(assignment);
                }
            }
        } else if (part.find("doc=") == 0) {
            for (const auto& assign_text : split(part.substr(4), ',')) {
                WorkloadAssignment assignment;
                if (parse_assignment(assign_text, assignment)) {
                    out.insert_document.set_field(assignment.field, assignment.value);
                }
            }
        } else if (part.find("project=") == 0) {
            out.projection = split(part.substr(8), ',');
        } else if (part.find("agg=") == 0) {
            out.aggregate_field = part.substr(4);
        } else if (part.find("limit=") == 0) {
            try {
                out.limit = std::stoi(part.substr(6));
            } catch (...) {
                out.limit = 0;
            }
        }
    }
    return true;
}

bool WorkloadParser::parse_predicate(const std::string& text, WorkloadPredicate& out) {
    size_t pos = text.find("!=");
    std::string op = "!=";
    if (pos == std::string::npos) {
        pos = text.find(">=");
        op = ">=";
    }
    if (pos == std::string::npos) {
        pos = text.find("<=");
        op = "<=";
    }
    if (pos == std::string::npos) {
        pos = text.find('=');
        op = "=";
    }
    if (pos == std::string::npos) {
        pos = text.find('>');
        op = ">";
    }
    if (pos == std::string::npos) {
        pos = text.find('<');
        op = "<";
    }
    if (pos == std::string::npos) return false;

    out.field = trim(text.substr(0, pos));
    out.value = parse_value(trim(text.substr(pos + op.size())));
    if (op == "=") out.op = QueryOp::EQ;
    else if (op == "!=") out.op = QueryOp::NEQ;
    else if (op == ">") out.op = QueryOp::GT;
    else if (op == "<") out.op = QueryOp::LT;
    else if (op == ">=") out.op = QueryOp::GT;
    else out.op = QueryOp::LT;
    return !out.field.empty();
}

bool WorkloadParser::parse_assignment(const std::string& text, WorkloadAssignment& out) {
    size_t eq = text.find('=');
    if (eq == std::string::npos) return false;
    out.field = trim(text.substr(0, eq));
    out.value = parse_value(trim(text.substr(eq + 1)));
    return !out.field.empty();
}

Variant WorkloadParser::parse_value(const std::string& text) {
    if (text == "true") return Variant(true);
    if (text == "false") return Variant(false);
    if (text.size() >= 2 && ((text.front() == '\'' && text.back() == '\'') ||
                             (text.front() == '"' && text.back() == '"'))) {
        return Variant(text.substr(1, text.size() - 2));
    }
    bool numeric = !text.empty();
    for (char c : text) {
        if (!std::isdigit(static_cast<unsigned char>(c)) && c != '-') {
            numeric = false;
            break;
        }
    }
    if (numeric) {
        try {
            return Variant(std::stoi(text));
        } catch (...) {
            return Variant(0);
        }
    }
    return Variant(text);
}

WorkloadPlanner::WorkloadPlanner(const Catalog& cat) : catalog(cat) {}

WorkloadPlan WorkloadPlanner::plan(const WorkloadStatement& statement) const {
    WorkloadPlan plan;
    plan.statement = statement;
    const TableSchema* schema = catalog.get_table(statement.table);
    if (!schema) {
        plan.diagnostics.push_back({statement.table, "unknown table"});
        return plan;
    }

    validate_statement(*schema, plan);
    if (!plan.diagnostics.empty()) {
        return plan;
    }

    if (statement.type == WorkloadStatementType::INSERT) {
        plan_insert(*schema, plan);
    } else if (statement.type == WorkloadStatementType::UPDATE) {
        plan_update(*schema, plan);
    } else if (statement.type == WorkloadStatementType::DELETE_ROWS) {
        plan_delete(*schema, plan);
    } else if (statement.type == WorkloadStatementType::AGGREGATE) {
        plan_aggregate(*schema, plan);
    } else {
        plan_read_like(*schema, plan);
    }
    plan.accepted = plan.diagnostics.empty();
    return plan;
}

std::vector<WorkloadPlan> WorkloadPlanner::plan_all(const std::vector<WorkloadStatement>& statements) const {
    std::vector<WorkloadPlan> plans;
    plans.reserve(statements.size());
    for (const auto& statement : statements) {
        plans.push_back(plan(statement));
    }
    return plans;
}

void WorkloadPlanner::validate_statement(const TableSchema& schema, WorkloadPlan& plan) const {
    const WorkloadStatement& statement = plan.statement;
    if (statement.type == WorkloadStatementType::INSERT) {
        auto issues = schema.validate_document(statement.insert_document);
        plan.diagnostics.insert(plan.diagnostics.end(), issues.begin(), issues.end());
    }
    for (const auto& predicate : statement.predicates) {
        const FieldDefinition* field = schema.find_field(predicate.field);
        if (!field) {
            plan.diagnostics.push_back({predicate.field, "predicate references unknown field"});
            continue;
        }
        if (!TableSchema::type_matches(field->kind, predicate.value.type)) {
            plan.diagnostics.push_back({predicate.field, "predicate value type does not match field"});
        }
    }
    for (const auto& assignment : statement.assignments) {
        const FieldDefinition* field = schema.find_field(assignment.field);
        if (!field) {
            plan.diagnostics.push_back({assignment.field, "assignment references unknown field"});
            continue;
        }
        if (!TableSchema::type_matches(field->kind, assignment.value.type)) {
            plan.diagnostics.push_back({assignment.field, "assignment value type does not match field"});
        }
    }
    for (const auto& field_name : statement.projection) {
        if (!schema.has_field(field_name)) {
            plan.diagnostics.push_back({field_name, "projection references unknown field"});
        }
    }
    if (!statement.aggregate_field.empty() && !schema.has_field(statement.aggregate_field)) {
        plan.diagnostics.push_back({statement.aggregate_field, "aggregate references unknown field"});
    }
}

void WorkloadPlanner::plan_insert(const TableSchema& schema, WorkloadPlan& plan) const {
    add_step(plan, {PlanStepType::VALIDATE_INPUT, schema.name(), "", {}, 1, 4});
    add_step(plan, {PlanStepType::MATERIALIZE_DEFAULTS, schema.name(), "", {}, 1, 2});
    for (const auto& index : schema.indexes()) {
        add_step(plan, {PlanStepType::INDEX_LOOKUP, schema.name(), index.name, index.fields, 1, index.unique ? 3u : 5u});
    }
}

void WorkloadPlanner::plan_read_like(const TableSchema& schema, WorkloadPlan& plan) const {
    const IndexDefinition* index = choose_index(schema, plan.statement.predicates);
    uint64_t base = estimate_base_rows(schema);
    if (index) {
        uint64_t rows = base;
        for (const auto& predicate : plan.statement.predicates) {
            rows = std::max<uint64_t>(1, rows / estimate_selectivity(predicate, index));
        }
        add_step(plan, {PlanStepType::INDEX_LOOKUP, schema.name(), index->name, index->fields, rows, rows + 8});
    } else {
        add_step(plan, {PlanStepType::TABLE_SCAN, schema.name(), "", {}, base, base * 4});
    }
    if (!plan.statement.predicates.empty()) {
        add_step(plan, {PlanStepType::FILTER, schema.name(), "", {}, std::max<uint64_t>(1, base / 4), base});
    }
    if (!plan.statement.projection.empty()) {
        add_step(plan, {PlanStepType::PROJECT, schema.name(), "", plan.statement.projection,
                        std::max<uint64_t>(1, base / 4), plan.statement.projection.size() * 2});
    }
}

void WorkloadPlanner::plan_update(const TableSchema& schema, WorkloadPlan& plan) const {
    plan_read_like(schema, plan);
    add_step(plan, {PlanStepType::APPLY_UPDATE, schema.name(), "", {}, std::max<uint64_t>(1, estimate_base_rows(schema) / 4),
                    plan.statement.assignments.size() * 5 + 3});
}

void WorkloadPlanner::plan_delete(const TableSchema& schema, WorkloadPlan& plan) const {
    plan_read_like(schema, plan);
    add_step(plan, {PlanStepType::DELETE_MARK, schema.name(), "", {}, std::max<uint64_t>(1, estimate_base_rows(schema) / 5), 7});
}

void WorkloadPlanner::plan_aggregate(const TableSchema& schema, WorkloadPlan& plan) const {
    plan_read_like(schema, plan);
    add_step(plan, {PlanStepType::AGGREGATE, schema.name(), "", {plan.statement.aggregate_field},
                    std::max<uint64_t>(1, estimate_base_rows(schema) / 8), 25});
}

const IndexDefinition* WorkloadPlanner::choose_index(const TableSchema& schema,
                                                     const std::vector<WorkloadPredicate>& predicates) const {
    const IndexDefinition* best = nullptr;
    size_t best_score = 0;
    for (const auto& index : schema.indexes()) {
        size_t score = 0;
        for (const auto& key : index.fields) {
            auto found = std::find_if(predicates.begin(), predicates.end(),
                [&](const WorkloadPredicate& predicate) {
                    return predicate.field == key && predicate.op == QueryOp::EQ;
                });
            if (found != predicates.end()) {
                score += index.unique ? 4 : 2;
            }
        }
        if (score > best_score) {
            best_score = score;
            best = schema.find_index(index.name);
        }
    }
    return best;
}

uint64_t WorkloadPlanner::estimate_base_rows(const TableSchema& schema) {
    uint64_t field_weight = std::max<size_t>(1, schema.fields().size());
    uint64_t index_weight = std::max<size_t>(1, schema.indexes().size());
    return 128 * field_weight * index_weight;
}

uint64_t WorkloadPlanner::estimate_selectivity(const WorkloadPredicate& predicate, const IndexDefinition* index) {
    if (!index) return 2;
    if (predicate.op == QueryOp::EQ) return index->unique ? 64 : 8;
    if (predicate.op == QueryOp::GT || predicate.op == QueryOp::LT) return 4;
    return 2;
}

void WorkloadPlanner::add_step(WorkloadPlan& plan, const PlanStep& step) {
    plan.steps.push_back(step);
    plan.total_cost += step.estimated_cost;
}

} // namespace FenrirDB

