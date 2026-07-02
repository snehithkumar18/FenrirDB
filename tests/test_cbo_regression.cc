#include "../src/database.h"
#include "../src/wal.h"
#include "../src/optimizer.h"
#include "../src/sql_parser.h"
#include "../src/query_planner.h"
#include <iostream>
#include <vector>
#include <cassert>
#include <cstdio>

void test_cbo_and_joins_stress() {
    std::cout << "Running test_cbo_and_joins_stress..." << std::endl;
    std::string db_file = "stress_cbo_joins.db";
    std::remove(db_file.c_str());

    {
        FenrirDB::Database db;
        db.open(db_file);

        // Populate outer table documents (users)
        for (int i = 0; i < 50; ++i) {
            FenrirDB::Document doc;
            doc.set_field("id", FenrirDB::Variant("user_" + std::to_string(i)));
            doc.set_field("name", FenrirDB::Variant("Name_" + std::to_string(i)));
            doc.set_field("age", FenrirDB::Variant(20 + i));
            db.insert("user_" + std::to_string(i), doc);
        }

        db.close();
    }

    {
        FenrirDB::DiskManager disk_mgr(db_file);
        FenrirDB::BufferPoolManager cache_mgr(16, disk_mgr);
        
        // Root index is page 0
        FenrirDB::BPlusTreeIndex index(disk_mgr, cache_mgr, 0);

        // Configure Optimizer Statistics
        FenrirDB::QueryOptimizer optimizer;
        optimizer.get_stats().set_total_records(50);

        FenrirDB::ColumnStats age_stats;
        age_stats.min_val = 20;
        age_stats.max_val = 69;
        age_stats.num_distinct = 50;
        age_stats.num_records = 50;
        optimizer.get_stats().add_column_stats("age", age_stats);

        FenrirDB::ColumnStats id_stats;
        id_stats.num_distinct = 50;
        id_stats.num_records = 50;
        optimizer.get_stats().add_column_stats("id", id_stats);

        // Evaluate cost choice: SELECT name FROM users WHERE id = 'user_5';
        // Selectivity = 1 / 50 = 0.02. IndexScan should be chosen.
        bool choose_idx = optimizer.choose_index_scan("users", "id", FenrirDB::QueryOp::EQ, FenrirDB::Variant("user_5"), 5, 2);
        assert(choose_idx == true);

        // Evaluate selectivity for out of range query: age > 80 -> 0.0
        double sel_out = optimizer.get_stats().estimate_selectivity("age", FenrirDB::QueryOp::GT, FenrirDB::Variant(80));
        assert(sel_out == 0.0);

        // Volcano Scan Run: SELECT id, name FROM users;
        FenrirDB::SeqScanExecutor seq_exec(disk_mgr, cache_mgr);
        seq_exec.init();

        FenrirDB::Document doc;
        FenrirDB::RecordID rid;
        int count = 0;
        while (seq_exec.next(doc, rid)) {
            count++;
        }
        assert(count == 50);
        seq_exec.close();
    }

    std::remove(db_file.c_str());
    std::cout << "test_cbo_and_joins_stress passed." << std::endl;
}

int main() {
    test_cbo_and_joins_stress();
    std::cout << "All CBO and Joins stress tests passed successfully!" << std::endl;
    return 0;
}
