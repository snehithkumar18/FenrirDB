#include "../src/lock_manager.h"
#include "../src/transaction_manager.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <vector>
#include <chrono>

void test_concurrent_locks_stress() {
    std::cout << "Running test_concurrent_locks_stress..." << std::endl;
    FenrirDB::LockManager lock_mgr;

    std::vector<std::thread> workers;
    for (int i = 1; i <= 8; ++i) {
        workers.emplace_back([&lock_mgr, i]() {
            uint32_t tx_id = i;
            uint32_t resource_id = (i % 2 == 0) ? 100 : 200;

            // Request Shared Lock
            bool s_locked = lock_mgr.acquire_shared(tx_id, resource_id);
            if (s_locked) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                lock_mgr.release(tx_id, resource_id);
            }
        });
    }

    for (auto& t : workers) {
        t.join();
    }
    std::cout << "test_concurrent_locks_stress passed." << std::endl;
}

void test_transaction_snapshot_visibility() {
    std::cout << "Running test_transaction_snapshot_visibility..." << std::endl;
    FenrirDB::LogManager log_mgr("dummy.log");
    FenrirDB::LockManager lock_mgr;
    FenrirDB::TransactionManager tx_mgr(log_mgr, lock_mgr);

    // Start tx1
    auto tx1 = tx_mgr.begin_tx(1);
    tx1->read_ts = 10;

    // Start tx2
    auto tx2 = tx_mgr.begin_tx(2);
    tx2->read_ts = 20;

    // Verify snapshot LSN visibility rules
    // A write at LSN 15 should be invisible to tx1 (read_ts = 10) but visible to tx2 (read_ts = 20)
    assert(tx_mgr.is_visible(1, 15) == false);
    assert(tx_mgr.is_visible(2, 15) == true);

    tx_mgr.commit_tx(1);
    tx_mgr.commit_tx(2);
    std::cout << "test_transaction_snapshot_visibility passed." << std::endl;
}

int main() {
    test_concurrent_locks_stress();
    test_transaction_snapshot_visibility();
    std::cout << "All MCT stress tests passed successfully!" << std::endl;
    return 0;
}
