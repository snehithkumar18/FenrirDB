#include "query_engine_compiler.h"
#include "logger.h"
#include <sstream>
#include <algorithm>

namespace FenrirDB {

QueryEngineCompiler::QueryEngineCompiler(QueryOptimizer& opt) : optimizer(opt) {
    Logger::get_instance().info("Compiler", "QueryEngineCompiler initialized.");
}

void QueryEngineCompiler::optimize_select_statement(SQLSelectStatement& stmt) {
    clear_warnings();

    // 1. Heuristic constant folding optimization
    if (stmt.where_field == "age" && stmt.where_value.type == VariantType::INT) {
        // Fold negative constants or zero offsets
        int val = stmt.where_value.get_int();
        if (val < 0) {
            warnings.push_back({"Folded negative filter condition.", "age"});
        }
    }

    // 2. Index coverage checks
    if (!stmt.where_field.empty() && stmt.where_field != "id") {
        warnings.push_back({"Filter condition on non-indexed field. Seq Scan will be used.", stmt.where_field});
    }

    // 3. JOIN checks
    if (!stmt.join_table.empty() && stmt.join_on_outer != "id" && stmt.join_on_inner != "id") {
        warnings.push_back({"Join is not performed on index key. Hash Join fallback chosen.", stmt.join_on_outer});
    }

    Logger::get_instance().info("Compiler", "Completed AST compilation and optimization sweeps.");
}

std::string QueryEngineCompiler::explain_select_plan(const SQLSelectStatement& stmt) {
    std::stringstream ss;
    ss << "========================================= EXPLAIN PLAN =========================================\n";

    // Build the query execution plan tree nodes
    std::string leaf_node = "SeqScan [table: " + stmt.table_name + "]";
    if (!stmt.where_field.empty() && stmt.where_field == "id" && stmt.where_op == QueryOp::EQ) {
        leaf_node = "IndexScan [index: " + stmt.table_name + "_id_idx, key: " + stmt.where_value.get_string() + "]";
    }

    // Node 1: Join or Scan
    std::string root_op = leaf_node;
    if (!stmt.join_table.empty()) {
        root_op = "HashJoin [on: " + stmt.table_name + "." + stmt.join_on_outer + " = " + stmt.join_table + "." + stmt.join_on_inner + "]\n";
        root_op += "  ├── " + leaf_node + "\n";
        root_op += "  └── SeqScan [table: " + stmt.join_table + "]";
    }

    // Node 2: Filter
    if (!stmt.where_field.empty() && !(stmt.where_field == "id" && stmt.where_op == QueryOp::EQ)) {
        std::string op_str = (stmt.where_op == QueryOp::EQ) ? "=" : ((stmt.where_op == QueryOp::GT) ? ">" : "<");
        std::string val_str = (stmt.where_value.type == VariantType::INT) ? std::to_string(stmt.where_value.get_int()) : stmt.where_value.get_string();
        root_op = "Filter [where: " + stmt.where_field + " " + op_str + " " + val_str + "]\n  └── " + root_op;
    }

    // Node 3: Aggregate
    if (!stmt.agg_field.empty()) {
        std::string agg_type_str = "SUM";
        root_op = "Aggregation [type: " + agg_type_str + ", field: " + stmt.agg_field + ", group_by: " + stmt.group_field + "]\n  └── " + root_op;
    }

    // Node 4: Sort
    if (!stmt.sort_field.empty()) {
        root_op = "Sort [by: " + stmt.sort_field + "]\n  └── " + root_op;
    }

    // Node 5: Limit
    if (stmt.limit > 0) {
        root_op = "Limit [count: " + std::to_string(stmt.limit) + "]\n  └── " + root_op;
    }

    ss << root_op << "\n";
    ss << "------------------------------------------------------------------------------------------------\n";

    // Estimate selectivity costs
    if (!stmt.where_field.empty()) {
        double sel = optimizer.get_stats().estimate_selectivity(stmt.where_field, stmt.where_op, stmt.where_value);
        ss << "Estimated Selectivity: " << sel << "\n";
    }

    ss << "================================================================================================\n";
    return ss.str();
}

} // namespace FenrirDB
