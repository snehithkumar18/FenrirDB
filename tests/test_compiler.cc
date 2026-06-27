#include "../src/query_engine_compiler.h"
#include <iostream>
#include <cassert>

void test_compiler_optimization_rules() {
    std::cout << "Running test_compiler_optimization_rules..." << std::endl;
    
    FenrirDB::QueryOptimizer optimizer;
    FenrirDB::QueryEngineCompiler compiler(optimizer);

    FenrirDB::SQLSelectStatement stmt;
    stmt.table_name = "employees";
    stmt.select_fields = {"name", "salary"};
    stmt.where_field = "age";
    stmt.where_op = FenrirDB::QueryOp::GT;
    stmt.where_value = FenrirDB::Variant(30);

    compiler.optimize_select_statement(stmt);
    std::vector<FenrirDB::CompilerWarning> warnings = compiler.get_warnings();

    // Verify warning for filtering non-indexed field is raised
    assert(!warnings.empty());
    assert(warnings[0].column == "age");

    std::cout << "test_compiler_optimization_rules passed." << std::endl;
}

void test_compiler_explain_graph() {
    std::cout << "Running test_compiler_explain_graph..." << std::endl;
    
    FenrirDB::QueryOptimizer optimizer;
    FenrirDB::QueryEngineCompiler compiler(optimizer);

    FenrirDB::SQLSelectStatement stmt;
    stmt.table_name = "users";
    stmt.select_fields = {"name"};
    stmt.where_field = "id";
    stmt.where_op = FenrirDB::QueryOp::EQ;
    stmt.where_value = FenrirDB::Variant("user_5");
    stmt.limit = 10;

    std::string explain = compiler.explain_select_plan(stmt);
    assert(!explain.empty());
    assert(explain.find("IndexScan") != std::string::npos);
    assert(explain.find("Limit") != std::string::npos);

    std::cout << "test_compiler_explain_graph passed." << std::endl;
}

int main() {
    test_compiler_optimization_rules();
    test_compiler_explain_graph();
    std::cout << "All Compiler tests passed successfully!" << std::endl;
    return 0;
}
