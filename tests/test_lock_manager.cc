#include "../src/lock_manager.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>

void test_shared_locks() {
    std::cout << "Running test_shared_locks..." << std::endl;
    FenrirDB::LockManager lm;
    FenrirDB::RecordID r1 = { 1, 1 };

    // Multiple transactions should be able to acquire shared locks on the same record
    DBErrorCode res1 = lm.acquire_shared(1, r1);
    DBErrorCode res2 = lm.acquire_shared(2, r1);
    DBErrorCode res3 = lm.acquire_shared(3, r1);

    assert(res1 == DBErrorCode::SUCCESS);
    assert(res2 == DBErrorCode::SUCCESS);
    assert(res3 == DBErrorCode::SUCCESS);

    lm.release(1, r1);
    lm.release(2, r1);
    lm.release(3, r1);
    std::cout << "test_shared_locks passed." << std::endl;
}

void test_exclusive_lock_blocking() {
    std::cout << "Running test_exclusive_lock_blocking..." << std::endl;
    FenrirDB::LockManager lm;
    FenrirDB::RecordID r1 = { 1, 1 };
    std::atomic<bool> thread_done(false);

    // Thread 1 acquires Exclusive lock
    lm.acquire_exclusive(1, r1);

    std::thread t([&]() {
        // Thread 2 attempts to acquire lock and should block
        lm.acquire_shared(2, r1);
        thread_done = true;
        lm.release(2, r1);
    });

    // Let the thread run for a bit, verifying it is blocked
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    assert(thread_done == false);

    // Thread 1 releases lock, Thread 2 should unblock and finish
    lm.release(1, r1);
    t.join();
    assert(thread_done == true);

    std::cout << "test_exclusive_lock_blocking passed." << std::endl;
}

void test_lock_upgrade() {
    std::cout << "Running test_lock_upgrade..." << std::endl;
    FenrirDB::LockManager lm;
    FenrirDB::RecordID r1 = { 1, 1 };

    // Upgrade Shared to Exclusive
    lm.acquire_shared(1, r1);
    DBErrorCode res = lm.acquire_exclusive(1, r1);
    assert(res == DBErrorCode::SUCCESS);

    lm.release(1, r1);
    std::cout << "test_lock_upgrade passed." << std::endl;
}

void test_deadlock_detection() {
    std::cout << "Running test_deadlock_detection..." << std::endl;
    FenrirDB::LockManager lm;
    FenrirDB::RecordID r1 = { 1, 1 };
    FenrirDB::RecordID r2 = { 2, 2 };

    // Transaction 1 locks r1
    lm.acquire_exclusive(1, r1);
    // Transaction 2 locks r2
    lm.acquire_exclusive(2, r2);

    std::thread t1([&]() {
        // Transaction 1 attempts to lock r2 (held by 2)
        lm.acquire_exclusive(1, r2);
        lm.release_all(1);
    });

    std::thread t2([&]() {
        // Transaction 2 attempts to lock r1 (held by 1)
        lm.acquire_exclusive(2, r1);
        lm.release_all(2);
    });

    // Wait for threads to enter block queues
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Run deadlock detector
    bool deadlock = lm.detect_deadlock();
    assert(deadlock == true);

    // Force release to prevent thread leaks in test runner
    lm.release_all(1);
    lm.release_all(2);
    t1.join();
    t2.join();

    std::cout << "test_deadlock_detection passed." << std::endl;
}

int main() {
    test_shared_locks();
    test_exclusive_lock_blocking();
    test_lock_upgrade();
    test_deadlock_detection();
    std::cout << "All LockManager tests passed successfully!" << std::endl;
    return 0;
}
