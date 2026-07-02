#include "../src/database.h"
#include "../src/errors.h"
#include "../src/wal.h"
#include "../src/lock_manager.h"
#include "../src/sql_parser.h"
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>
#include <random>
#include <cassert>
#include <chrono>

using FenrirDB::DBErrorCode;

constexpr int STRESS_PAGES = 200;
constexpr int STRESS_ITERATIONS = 1000;
constexpr int STRESS_THREADS = 8;

std::atomic<bool> stress_running(true);
std::atomic<int> completed_threads(0);

// ======================================================================
// Stress Test 1: Page Cache Thrashing
// ======================================================================
void run_cache_thrashing(FenrirDB::BufferPoolManager& cache_mgr, FenrirDB::DiskManager& disk_mgr, int thread_id) {
    std::mt19937 rng(1337 + thread_id);
    std::uniform_int_distribution<uint32_t> dist(0, STRESS_PAGES - 1);

    for (int i = 0; i < STRESS_ITERATIONS; ++i) {
        uint32_t page_id = dist(rng);
        FenrirDB::Page* page = cache_mgr.fetch_page(page_id);
        if (page) {
            // Write mock data to slots
            std::string data = "Thread " + std::to_string(thread_id) + " write " + std::to_string(i);
            page->insert_record(0, reinterpret_cast<const uint8_t*>(data.c_str()), data.size());
            cache_mgr.flush_page(page_id);
        }
    }
}

void test_cache_stress() {
    std::cout << "Starting cache stress tests..." << std::endl;
    std::string db_file = "stress_cache.db";
    std::remove(db_file.c_str());

    {
        FenrirDB::DiskManager disk_mgr(db_file);
        FenrirDB::BufferPoolManager cache_mgr(16, disk_mgr); // Pool of 16 pages

        // Allocate pages
        for (int i = 0; i < STRESS_PAGES; ++i) {
            disk_mgr.allocate_page();
        }

        std::vector<std::thread> threads;
        for (int i = 0; i < STRESS_THREADS; ++i) {
            threads.emplace_back(run_cache_thrashing, std::ref(cache_mgr), std::ref(disk_mgr), i);
        }

        for (auto& t : threads) {
            t.join();
        }
        cache_mgr.flush_all();
    }

    std::remove(db_file.c_str());
    std::cout << "Cache stress tests passed." << std::endl;
}

// ======================================================================
// Stress Test 2: B+ Tree Insertion and Range Queries
// ======================================================================
void test_index_stress() {
    std::cout << "Starting index stress tests..." << std::endl;
    std::string db_file = "stress_index.db";
    std::remove(db_file.c_str());

    {
        FenrirDB::DiskManager disk_mgr(db_file);
        FenrirDB::BufferPoolManager cache_mgr(32, disk_mgr);

        uint32_t root_id = disk_mgr.allocate_page();
        FenrirDB::Page* root_page = cache_mgr.fetch_page(root_id);
        if (root_page) {
            FenrirDB::IndexNode* node = reinterpret_cast<FenrirDB::IndexNode*>(root_page->data + 8);
            new (node) FenrirDB::IndexNode();
            node->is_leaf = true;
            node->num_keys = 0;
            cache_mgr.flush_page(root_id);
        }

        FenrirDB::BPlusTreeIndex idx(disk_mgr, cache_mgr, root_id);

        // Insert thousands of keys
        int num_inserts = 2000;
        for (int i = 0; i < num_inserts; ++i) {
            std::string key = "key_" + std::to_string(i);
            FenrirDB::RecordID rid = { static_cast<uint32_t>(i), 0 };
            DBErrorCode res = idx.insert(key, rid);
            assert(res == DBErrorCode::SUCCESS);
        }

        // Search keys
        for (int i = 0; i < num_inserts; ++i) {
            std::string key = "key_" + std::to_string(i);
            FenrirDB::RecordID rid;
            DBErrorCode res = idx.search(key, rid);
            assert(res == DBErrorCode::SUCCESS);
            assert(rid.page_id == static_cast<uint32_t>(i));
        }

        // Range query stress
        std::vector<FenrirDB::RecordID> results;
        DBErrorCode res = idx.range_search("key_100", "key_105", results);
        assert(res == DBErrorCode::SUCCESS);
    }

    std::remove(db_file.c_str());
    std::cout << "Index stress tests passed." << std::endl;
}

// ======================================================================
// Stress Test 3: SQL Parsing Workload
// ======================================================================
void test_sql_parser_stress() {
    std::cout << "Starting SQL parser stress tests..." << std::endl;

    std::vector<std::string> sql_queries = {
        "SELECT name, age FROM users WHERE age > 21;",
        "SELECT id FROM products WHERE price < 100;",
        "INSERT INTO users user1 (name, age, active) VALUES ('Alice', 30, true);",
        "INSERT INTO log l2 (event, code) VALUES ('fault', 500);",
        "SELECT * FROM accounts WHERE balance = 5000;"
    };

    for (int i = 0; i < 200; ++i) {
        for (const auto& sql : sql_queries) {
            FenrirDB::SQLLexer lexer(sql);
            std::vector<FenrirDB::Token> tokens = lexer.tokenize();
            assert(!tokens.empty());

            FenrirDB::SQLParser parser(tokens);
            auto stmt = parser.parse();
            if (stmt) {
                assert(stmt->type == FenrirDB::StatementType::SELECT || stmt->type == FenrirDB::StatementType::INSERT);
            }
        }
    }
    std::cout << "SQL parser stress tests passed." << std::endl;
}

// ======================================================================
// Stress Test 4: Concurrency Lock Manager Conflicts
// ======================================================================
void run_lock_worker(FenrirDB::LockManager& lm, int thread_id) {
    std::mt19937 rng(42 + thread_id);
    std::uniform_int_distribution<uint32_t> page_dist(1, 10);
    std::uniform_int_distribution<uint16_t> slot_dist(1, 5);
    std::uniform_int_distribution<int> mode_dist(0, 1);

    for (int i = 0; i < 500; ++i) {
        FenrirDB::RecordID r1 = { page_dist(rng), slot_dist(rng) };
        bool is_exclusive = (mode_dist(rng) == 1);

        if (is_exclusive) {
            lm.acquire_exclusive(thread_id, r1);
        } else {
            lm.acquire_shared(thread_id, r1);
        }

        std::this_thread::sleep_for(std::chrono::microseconds(50));
        lm.release(thread_id, r1);
    }
    completed_threads++;
}

void test_lock_stress() {
    std::cout << "Starting lock manager stress tests..." << std::endl;
    FenrirDB::LockManager lm;

    std::vector<std::thread> threads;
    for (int i = 0; i < STRESS_THREADS; ++i) {
        threads.emplace_back(run_lock_worker, std::ref(lm), i);
    }

    // Monitor for deadlocks periodically
    while (completed_threads < STRESS_THREADS) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        lm.detect_deadlock();
    }

    for (auto& t : threads) {
        t.join();
    }
    std::cout << "Lock manager stress tests passed." << std::endl;
}

int main() {
    auto start = std::chrono::high_resolution_clock::now();

    test_cache_stress();
    test_index_stress();
    test_sql_parser_stress();
    test_lock_stress();

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;
    std::cout << "All stress tests completed successfully in " << diff.count() << " seconds!" << std::endl;

    return 0;
}
