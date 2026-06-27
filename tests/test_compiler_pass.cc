#include "../src/query_engine_compiler_pass.h"
#include <iostream>
#include <cassert>

void test_folding_pass() {
    std::cout << "Running test_folding_pass..." << std::endl;
    FenrirDB::ConstantFoldingPass pass;

    FenrirDB::SQLSelectStatement stmt;
    stmt.where_field = "salary";
    stmt.where_value = FenrirDB::Variant(-500);

    pass.run(stmt);
    assert(stmt.where_value.get_int() == 0); // Folded to 0

    std::cout << "test_folding_pass passed." << std::endl;
}

void test_pushdown_pass() {
    std::cout << "Running test_pushdown_pass..." << std::endl;
    FenrirDB::PredicatePushdownPass pass;

    FenrirDB::SQLSelectStatement stmt;
    stmt.where_field = "id";
    stmt.where_op = FenrirDB::QueryOp::EQ;
    stmt.limit = 5;

    pass.run(stmt);
    assert(stmt.limit == 1); // Reduced to 1

    std::cout << "test_pushdown_pass passed." << std::endl;
}

void test_pipeline_execution() {
    std::cout << "Running test_pipeline_execution..." << std::endl;
    FenrirDB::OptimizationPipeline pipeline;

    FenrirDB::SQLSelectStatement stmt;
    stmt.where_field = "id";
    stmt.where_op = FenrirDB::QueryOp::EQ;
    stmt.limit = 10;

    pipeline.execute(stmt);
    assert(stmt.limit == 1); // Unique ID limit pushdown check

    std::cout << "test_pipeline_execution passed." << std::endl;
}

int main() {
    test_folding_pass();
    test_pushdown_pass();
    test_pipeline_execution();
    std::cout << "All Compiler Pass tests passed successfully!" << std::endl;
    return 0;
}
