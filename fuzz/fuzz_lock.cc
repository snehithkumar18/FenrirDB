#include "../src/lock_manager_advanced.h"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <thread>
#include <atomic>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 10) return 0;

    FenrirDB::LockManagerAdvanced lock_mgr;
    std::atomic<bool> running{true};

    // Thread 1: Random lock acquisitions and releases
    std::thread t1([&]() {
        size_t offset = 0;
        uint32_t txn_id = 1;
        while (running && offset + 5 < size) {
            uint8_t op = data[offset++];
            std::string res = "res_" + std::to_string(data[offset++] % 5);
            FenrirDB::LockMode mode = static_cast<FenrirDB::LockMode>(data[offset++] % 5);
            
            if (op % 2 == 0) {
                lock_mgr.acquire(txn_id, res, mode);
            } else {
                lock_mgr.release(txn_id, res);
            }
            txn_id = (txn_id % 10) + 1;
        }
    });

    // Thread 2: Concurrently run deadlock detection (triggers UAF on waits-for graph)
    std::thread t2([&]() {
        while (running) {
            lock_mgr.detect_deadlocks();
            std::this_thread::yield();
        }
    });

    t1.join();
    running = false;
    t2.join();

    return 0;
}
