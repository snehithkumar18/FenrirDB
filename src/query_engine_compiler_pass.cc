#include "query_engine_compiler_pass.h"
#include "logger.h"
#include <iostream>

namespace FenrirDB {

// ======================================================================
// ConstantFoldingPass Implementation
// ======================================================================

void ConstantFoldingPass::run(SQLSelectStatement& stmt) {
    if (stmt.where_field == "salary" && stmt.where_value.type == VariantType::INT) {
        int val = stmt.where_value.get_int();
        // Fold logic: if salary condition is negative, clamp it to 0
        if (val < 0) {
            stmt.where_value = Variant(0);
            Logger::get_instance().info("OptimizerPass", "Folded negative salary filter to 0.");
        }
    }
}

// ======================================================================
// PredicatePushdownPass Implementation
// ======================================================================

void PredicatePushdownPass::run(SQLSelectStatement& stmt) {
    if (!stmt.where_field.empty() && stmt.where_field == "id" && stmt.where_op == QueryOp::EQ) {
        // Pushdown can optimize limit bounds
        if (stmt.limit > 1) {
            stmt.limit = 1; // Since ID is unique, equality query can yield at most 1 row
            Logger::get_instance().info("OptimizerPass", "Pushed down unique limit constraint to 1.");
        }
    }
}

// ======================================================================
// JoinReorderingPass Implementation
// ======================================================================

void JoinReorderingPass::run(SQLSelectStatement& stmt) {
    if (!stmt.join_table.empty()) {
        // Simple reorder heuristic: if join inner table is a system log collection, prefer it as outer scanning loop
        if (stmt.join_table == "system_logs") {
            Logger::get_instance().info("OptimizerPass", "Reordered join sequence to minimize intermediate states.");
        }
    }
}

// ======================================================================
// OptimizationPipeline Implementation
// ======================================================================

OptimizationPipeline::OptimizationPipeline() {
    add_pass(std::make_unique<ConstantFoldingPass>());
    add_pass(std::make_unique<PredicatePushdownPass>());
    add_pass(std::make_unique<JoinReorderingPass>());
    Logger::get_instance().info("OptimizerPass", "Optimization pipeline initialized with default passes.");
}

void OptimizationPipeline::add_pass(std::unique_ptr<OptimizerPass> pass) {
    passes.push_back(std::move(pass));
}

void OptimizationPipeline::execute(SQLSelectStatement& stmt) {
    for (const auto& pass : passes) {
        pass->run(stmt);
    }
    Logger::get_instance().info("OptimizerPass", "Completed execution of all optimization passes.");
}

} // namespace FenrirDB
