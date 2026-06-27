#include "../src/wal.h"
#include "../src/storage.h"
#include "../src/cache.h"
#include <iostream>
#include <vector>
#include <random>
#include <cassert>
#include <cstdio>

void simulate_crash_recovery(int num_transactions, int seed) {
    std::string db_file = "stress_recovery_" + std::to_string(seed) + ".db";
    std::string log_file = "stress_recovery_" + std::to_string(seed) + ".log";
    std::remove(db_file.c_str());
    std::remove(log_file.c_str());

    std::vector<std::string> keys;
    std::vector<std::string> committed_keys;
    std::vector<std::string> aborted_keys;

    // Phase 1: Simulate active updates and intermittent crashes
    {
        FenrirDB::DiskManager disk_mgr(db_file);
        FenrirDB::BufferPoolManager cache_mgr(10, disk_mgr);
        FenrirDB::LogManager log_mgr(log_file);

        uint32_t doc_page = disk_mgr.allocate_page();
        FenrirDB::Page* page = cache_mgr.fetch_page(doc_page);
        new (page) FenrirDB::Page(doc_page);

        std::mt19937 rng(seed);
        std::uniform_int_distribution<int> action_dist(0, 1); // 0=Commit, 1=Abort/Crash

        for (int tx = 1; tx <= num_transactions; ++tx) {
            log_mgr.append_record(tx, FenrirDB::LogRecordType::BEGIN);
            
            std::string key = "key_" + std::to_string(tx);
            std::vector<uint8_t> val = { static_cast<uint8_t>('a' + (tx % 26)) };
            uint16_t slot = tx % 100;

            page->insert_record(slot, val.data(), val.size());
            log_mgr.append_record(tx, FenrirDB::LogRecordType::INSERT, doc_page, slot, {}, val);

            int outcome = action_dist(rng);
            if (outcome == 0) {
                log_mgr.append_record(tx, FenrirDB::LogRecordType::COMMIT);
                committed_keys.push_back(key);
            } else {
                // Intermittent crash simulation: we leave the log without a COMMIT,
                // meaning this transaction's changes should be discarded during recovery.
                aborted_keys.push_back(key);
            }
        }
        cache_mgr.flush_all();
    }

    // Phase 2: Run Recovery and reconcile states
    {
        FenrirDB::DiskManager disk_mgr(db_file);
        FenrirDB::BufferPoolManager cache_mgr(10, disk_mgr);
        FenrirDB::LogManager log_mgr(log_file);
        FenrirDB::RecoveryManager rm(log_mgr, disk_mgr);

        DBErrorCode res = rm.recover(cache_mgr);
        assert(res == DBErrorCode::SUCCESS);

        FenrirDB::Page* page = cache_mgr.fetch_page(0);
        assert(page != nullptr);

        // Verify committed transaction keys are present
        for (const auto& key : committed_keys) {
            int tx = std::stoi(key.substr(4));
            uint16_t slot = tx % 100;
            std::vector<uint8_t> val_bytes;
            DBErrorCode get_res = page->get_record(slot, val_bytes);
            assert(get_res == DBErrorCode::SUCCESS);
            assert(val_bytes.size() == 1);
            assert(val_bytes[0] == static_cast<uint8_t>('a' + (tx % 26)));
        }

        // Verify aborted transaction keys are rolled back
        for (const auto& key : aborted_keys) {
            int tx = std::stoi(key.substr(4));
            uint16_t slot = tx % 100;
            std::vector<uint8_t> val_bytes;
            DBErrorCode get_res = page->get_record(slot, val_bytes);
            assert(get_res == DBErrorCode::ERR_RECORD_NOT_FOUND);
        }
    }

    std::remove(db_file.c_str());
    std::remove(log_file.c_str());
}

int main() {
    std::cout << "Starting transaction recovery stress tests..." << std::endl;
    for (int seed = 1; seed <= 5; ++seed) {
        simulate_crash_recovery(50, seed);
    }
    std::cout << "All transaction recovery stress tests passed successfully!" << std::endl;
    return 0;
}
