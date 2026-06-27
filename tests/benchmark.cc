#include "../src/database.h"
#include "../src/wal.h"
#include "../src/lock_manager.h"
#include "../src/optimizer.h"
#include "../src/sql_parser.h"
#include "../src/query_planner.h"
#include <iostream>
#include <chrono>
#include <vector>
#include <thread>
#include <random>
#include <cmath>
#include <iomanip>
#include <cstdio>

// Helper to format benchmark tables
void print_header(const std::string& title) {
    std::cout << "\n======================================================================\n";
    std::cout << " BENCHMARK: " << title << "\n";
    std::cout << "======================================================================\n";
}

void print_footer() {
    std::cout << "----------------------------------------------------------------------\n";
}

// ======================================================================
// Benchmark 1: Sequential inserts performance
// ======================================================================
void benchmark_inserts(int num_records) {
    print_header("Sequential Record Insertion Latency");
    std::string db_file = "bench_insert.db";
    std::remove(db_file.c_str());

    FenrirDB::Database db;
    db.open(db_file);

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < num_records; ++i) {
        FenrirDB::Document doc;
        doc.set_field("name", FenrirDB::Variant("User_" + std::to_string(i)));
        doc.set_field("age", FenrirDB::Variant(20 + (i % 60)));
        doc.set_field("active", FenrirDB::Variant(i % 2 == 0));
        db.insert("key_" + std::to_string(i), doc);
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;

    double ops_sec = static_cast<double>(num_records) / diff.count();
    double avg_latency = (diff.count() / static_cast<double>(num_records)) * 1000.0;

    std::cout << std::left << std::setw(25) << "Records Inserted:" << num_records << "\n";
    std::cout << std::left << std::setw(25) << "Total Duration:" << std::fixed << std::setprecision(4) << diff.count() << " seconds\n";
    std::cout << std::left << std::setw(25) << "Throughput:" << std::fixed << std::setprecision(2) << ops_sec << " ops/sec\n";
    std::cout << std::left << std::setw(25) << "Average Latency:" << std::fixed << std::setprecision(4) << avg_latency << " ms/op\n";
    
    db.close();
    std::remove(db_file.c_str());
    print_footer();
}

// ======================================================================
// Benchmark 2: B+ Tree Index Search vs Seq Table Scans
// ======================================================================
void benchmark_scans(int num_records, int num_lookups) {
    print_header("B+ Tree Index Scan vs Sequential Table Scans");
    std::string db_file = "bench_scan.db";
    std::remove(db_file.c_str());

    FenrirDB::Database db;
    db.open(db_file);

    // Populate database
    for (int i = 0; i < num_records; ++i) {
        FenrirDB::Document doc;
        doc.set_field("id", FenrirDB::Variant("id_" + std::to_string(i)));
        doc.set_field("value", FenrirDB::Variant(i));
        db.insert("id_" + std::to_string(i), doc);
    }

    // Benchmark 2a: Sequential Scans using query planner filter
    auto start_seq = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < num_lookups; ++i) {
        std::vector<FenrirDB::Document> results;
        // Forces sequential scan since it filters on "value" instead of "id"
        db.query("value = " + std::to_string(i % num_records), results);
    }
    auto end_seq = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff_seq = end_seq - start_seq;

    // Benchmark 2b: Index Scans via key get
    auto start_idx = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < num_lookups; ++i) {
        FenrirDB::Document doc;
        db.get("id_" + std::to_string(i % num_records), doc);
    }
    auto end_idx = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff_idx = end_idx - start_idx;

    std::cout << std::left << std::setw(30) << "Database Size:" << num_records << " records\n";
    std::cout << std::left << std::setw(30) << "Lookups Executed:" << num_lookups << "\n";
    std::cout << std::left << std::setw(30) << "Seq Table Scan Duration:" << std::fixed << std::setprecision(4) << diff_seq.count() << " seconds\n";
    std::cout << std::left << std::setw(30) << "B+ Tree Index Scan Duration:" << std::fixed << std::setprecision(4) << diff_idx.count() << " seconds\n";
    
    double speedup = diff_seq.count() / diff_idx.count();
    std::cout << std::left << std::setw(30) << "Index Speedup Factor:" << std::fixed << std::setprecision(2) << speedup << "x faster\n";

    db.close();
    std::remove(db_file.c_str());
    print_footer();
}

// ======================================================================
// Benchmark 3: Buffer Pool Cache Eviction Sizing Hit Rates
// ======================================================================
void benchmark_cache_sizing(int num_pages, int num_lookups) {
    print_header("Buffer Pool Size vs Cache Hit Rates");
    std::string db_file = "bench_cache.db";
    std::remove(db_file.c_str());

    FenrirDB::DiskManager disk_mgr(db_file);
    // Allocate pages on disk
    for (int i = 0; i < num_pages; ++i) {
        disk_mgr.allocate_page();
    }

    std::vector<int> cache_sizes = { 2, 4, 8, 16, 32, 64 };
    
    std::cout << std::right << std::setw(15) << "Cache Size" 
              << std::setw(20) << "Duration (s)" 
              << std::setw(20) << "Fetch Type" << "\n";
    std::cout << "----------------------------------------------------------------------\n";

    std::mt19937 rng(42);
    // Zipfian-like skew distribution: simulate hot pages
    std::exponential_distribution<double> exp_dist(1.5);

    for (int cache_size : cache_sizes) {
        FenrirDB::BufferPoolManager cache_mgr(cache_size, disk_mgr);
        
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < num_lookups; ++i) {
            // Generate skewed page access
            double rand_val = exp_dist(rng);
            int page_idx = static_cast<int>(rand_val * 10) % num_pages;
            cache_mgr.fetch_page(page_idx);
        }
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = end - start;

        std::cout << std::right << std::setw(15) << cache_size 
                  << std::setw(20) << std::fixed << std::setprecision(5) << diff.count()
                  << std::setw(20) << "Random-Skewed" << "\n";
    }

    std::remove(db_file.c_str());
    print_footer();
}

// ======================================================================
// Benchmark 4: Multi-threaded Concurrency Locks Scaling
// ======================================================================
void run_concurrency_worker(FenrirDB::LockManager& lm, int num_ops, int thread_id) {
    std::mt19937 rng(1234 + thread_id);
    std::uniform_int_distribution<uint32_t> page_dist(1, 50);
    std::uniform_int_distribution<uint16_t> slot_dist(1, 10);
    std::uniform_int_distribution<int> mode_dist(0, 4); // 80% read workload

    for (int i = 0; i < num_ops; ++i) {
        FenrirDB::RecordID rid = { page_dist(rng), slot_dist(rng) };
        bool exclusive = (mode_dist(rng) == 0); // 1 in 5 is writer

        if (exclusive) {
            lm.acquire_exclusive(thread_id, rid);
        } else {
            lm.acquire_shared(thread_id, rid);
        }

        // Simulating processing overhead
        std::this_thread::sleep_for(std::chrono::microseconds(10));
        lm.release(thread_id, rid);
    }
}

void benchmark_concurrency(int num_ops) {
    print_header("Lock Manager scaling (Throughput vs Threads)");
    
    std::vector<int> thread_counts = { 1, 2, 4, 8 };
    std::cout << std::right << std::setw(15) << "Threads Count" 
              << std::setw(20) << "Total Ops" 
              << std::setw(20) << "Duration (s)" 
              << std::setw(20) << "Kilo-Ops/Sec" << "\n";
    std::cout << "----------------------------------------------------------------------\n";

    for (int threads_num : thread_counts) {
        FenrirDB::LockManager lm;
        int ops_per_thread = num_ops / threads_num;

        auto start = std::chrono::high_resolution_clock::now();
        std::vector<std::thread> workers;
        for (int i = 0; i < threads_num; ++i) {
            workers.emplace_back(run_concurrency_worker, std::ref(lm), ops_per_thread, i);
        }
        for (auto& t : workers) {
            t.join();
        }
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> diff = end - start;

        double kops = (static_cast<double>(num_ops) / 1000.0) / diff.count();

        std::cout << std::right << std::setw(15) << threads_num 
                  << std::setw(20) << num_ops 
                  << std::setw(20) << std::fixed << std::setprecision(4) << diff.count()
                  << std::setw(20) << std::fixed << std::setprecision(2) << kops << "\n";
    }
    print_footer();
}

int main() {
    std::cout << "Starting FenrirDB Micro-Benchmarks...\n";
    
    benchmark_inserts(5000);
    benchmark_scans(500, 1000);
    benchmark_cache_sizing(100, 5000);
    benchmark_concurrency(10000);

    std::cout << "\nAll database micro-benchmarks completed successfully!\n";
    return 0;
}
