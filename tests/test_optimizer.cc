#include "../src/optimizer.h"
#include <iostream>
#include <cassert>
#include <cmath>

void test_selectivity_estimation() {
    std::cout << "Running test_selectivity_estimation..." << std::endl;
    FenrirDB::TableStats stats;
    stats.set_total_records(1000);

    FenrirDB::ColumnStats col_age;
    col_age.min_val = 10;
    col_age.max_val = 90;
    col_age.num_distinct = 80;
    col_age.num_records = 1000;
    stats.add_column_stats("age", col_age);

    // Test range selectivity estimation: age > 50 -> (90 - 50) / 80 = 40 / 80 = 0.5
    double sel_gt = stats.estimate_selectivity("age", FenrirDB::QueryOp::GT, FenrirDB::Variant(50));
    assert(std::abs(sel_gt - 0.5) < 0.001);

    // Test equality selectivity estimation: age = 20 -> 1 / 80 = 0.0125
    double sel_eq = stats.estimate_selectivity("age", FenrirDB::QueryOp::EQ, FenrirDB::Variant(20));
    assert(std::abs(sel_eq - 0.0125) < 0.001);

    std::cout << "test_selectivity_estimation passed." << std::endl;
}

void test_optimizer_cbo_decisions() {
    std::cout << "Running test_optimizer_cbo_decisions..." << std::endl;
    FenrirDB::QueryOptimizer optimizer;
    optimizer.get_stats().set_total_records(10000); // Large dataset

    FenrirDB::ColumnStats col_id;
    col_id.num_distinct = 10000; // Unique IDs
    col_id.num_records = 10000;
    optimizer.get_stats().add_column_stats("id", col_id);

    // Equality query selectivity = 1 / 10000 = 0.0001 (very selective)
    // CBO should choose Index Scan over Seq Scan
    bool choose_idx = optimizer.choose_index_scan("users", "id", FenrirDB::QueryOp::EQ, FenrirDB::Variant("user001"), 100, 3);
    assert(choose_idx == true);

    // If selectivity is high (default 0.3 for unknown columns), Seq Scan is cheaper
    bool choose_seq = optimizer.choose_index_scan("users", "unknown_col", FenrirDB::QueryOp::GT, FenrirDB::Variant(50), 50, 3);
    assert(choose_seq == false);

    std::cout << "test_optimizer_cbo_decisions passed." << std::endl;
}

int main() {
    test_selectivity_estimation();
    test_optimizer_cbo_decisions();
    std::cout << "All Query Optimizer tests passed successfully!" << std::endl;
    return 0;
}
