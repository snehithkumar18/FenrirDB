#include "../src/database.h"
#include "../src/wal_buffer.h"
#include "../src/checkpoint.h"
#include "../src/optimizer.h"
#include "../src/query_planner.h"
#include <iostream>
#include <vector>
#include <cassert>
#include <chrono>
#include <thread>
#include <cstdio>

// Helper to construct a large document with 10 fields
FenrirDB::Document make_large_mock_doc(int id_num, const std::string& name, int age, int salary) {
    FenrirDB::Document doc;
    doc.set_field("id", FenrirDB::Variant(id_num));
    doc.set_field("name", FenrirDB::Variant(name));
    doc.set_field("age", FenrirDB::Variant(age));
    doc.set_field("salary", FenrirDB::Variant(salary));
    doc.set_field("bonus", FenrirDB::Variant(salary / 10));
    doc.set_field("dept", FenrirDB::Variant((id_num % 2 == 0) ? "Engineering" : "Sales"));
    doc.set_field("active", FenrirDB::Variant(true));
    doc.set_field("rating", FenrirDB::Variant(5));
    doc.set_field("years", FenrirDB::Variant(id_num % 10));
    doc.set_field("city", FenrirDB::Variant("San Francisco"));
    return doc;
}

void run_heavy_insert_scan_workload() {
    std::cout << "Starting heavy insert and scan stress tests..." << std::endl;
    std::string db_file = "perf_stress_heavy.db";
    std::remove(db_file.c_str());

    {
        FenrirDB::Database db;
        db.open(db_file);

        auto start = std::chrono::high_resolution_clock::now();

        // 1. Insert 1000 large records
        for (int i = 1; i <= 1000; ++i) {
            std::string name = "Employee_" + std::to_string(i);
            int age = 20 + (i % 50);
            int salary = 40000 + (i * 10);
            FenrirDB::Document doc = make_large_mock_doc(i, name, age, salary);
            assert(db.insert("emp_" + std::to_string(i), doc) == DBErrorCode::SUCCESS);
        }

        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> diff = end - start;
        std::cout << "Inserted 1000 documents in " << diff.count() << " ms." << std::endl;

        // 2. Perform 500 lookups
        for (int i = 1; i <= 1000; i += 2) {
            FenrirDB::Document doc;
            assert(db.get("emp_" + std::to_string(i), doc) == DBErrorCode::SUCCESS);
            
            FenrirDB::Variant val;
            assert(doc.get_field("id", val) && val.get_int() == i);
            assert(doc.get_field("city", val) && val.get_string() == "San Francisco");
        }

        db.close();
    }

    std::remove(db_file.c_str());
    std::cout << "run_heavy_insert_scan_workload completed successfully." << std::endl;
}

void run_mvcc_wal_checkpoint_stress() {
    std::cout << "Starting MVCC transaction checkpoint stress tests..." << std::endl;
    std::string db_file = "perf_stress_mvcc.db";
    std::remove(db_file.c_str());

    FenrirDB::DiskManager disk_mgr(db_file);
    FenrirDB::BufferPoolManager cache_mgr(20, disk_mgr);
    FenrirDB::LogManager log_mgr(db_file + ".log");
    FenrirDB::LockManager lock_mgr;
    FenrirDB::TransactionManager tx_mgr(log_mgr, lock_mgr);
    FenrirDB::CheckpointManager cp_mgr(log_mgr, cache_mgr, tx_mgr);

    // Setup collections metadata
    uint32_t root_id = disk_mgr.allocate_page();
    FenrirDB::Page* root_page = cache_mgr.fetch_page(root_id);
    if (root_page) {
        FenrirDB::IndexNode* node = reinterpret_cast<FenrirDB::IndexNode*>(root_page->data + 8);
        new (node) FenrirDB::IndexNode();
        node->is_leaf = true;
        cache_mgr.flush_page(root_id);
    }
    FenrirDB::BPlusTreeIndex index(disk_mgr, cache_mgr, root_id);

    // Simulating parallel transaction pipelines
    std::vector<std::thread> workers;
    for (int t = 1; t <= 4; ++t) {
        workers.emplace_back([&, t]() {
            for (int i = 1; i <= 50; ++i) {
                uint32_t tx_id = t * 100 + i;
                auto tx = tx_mgr.begin_tx(tx_id);

                // Insert index pointer
                FenrirDB::CompositeKey key("tx_key_" + std::to_string(tx_id));
                FenrirDB::RecordID rid = { 100, static_cast<uint16_t>(tx_id) };
                assert(index.insert(key, rid) == DBErrorCode::SUCCESS);

                // Register WAL record
                log_mgr.append_record(tx_id, FenrirDB::LogRecordType::INSERT, 100, static_cast<uint16_t>(tx_id), {}, {1, 2, 3});

                if (i % 5 == 0) {
                    tx_mgr.abort_tx(tx_id);
                } else {
                    tx_mgr.commit_tx(tx_id);
                }
            }
        });
    }

    for (auto& worker : workers) {
        worker.join();
    }

    // Trigger Fuzzy Checkpoint to compact page log directory
    uint64_t safe_lsn = cp_mgr.begin_checkpoint();
    assert(safe_lsn > 0);

    std::remove(db_file.c_str());
    std::remove((db_file + ".log").c_str());
    std::cout << "run_mvcc_wal_checkpoint_stress completed successfully." << std::endl;
}

int main() {
    run_heavy_insert_scan_workload();
    run_mvcc_wal_checkpoint_stress();
    std::cout << "All Performance Stress tests passed successfully!" << std::endl;
    return 0;
}
