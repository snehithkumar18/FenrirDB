#include "../src/wal_buffer.h"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include <vector>
#include <cstdio>

void test_ring_buffer_wrapping() {
    std::cout << "Running test_ring_buffer_wrapping..." << std::endl;
    FenrirDB::LogRingBuffer ring;

    std::vector<uint8_t> data1(1000, 0xA);
    std::vector<uint8_t> data2(2000, 0xB);

    // Write to ring buffer
    assert(ring.write(data1.data(), data1.size()) == true);
    assert(ring.get_size() == 1000);

    assert(ring.write(data2.data(), data2.size()) == true);
    assert(ring.get_size() == 3000);

    // Read and verify
    std::vector<uint8_t> dest(3000);
    size_t bytes_read = ring.read(dest.data(), dest.size());
    assert(bytes_read == 3000);
    assert(ring.get_size() == 0);

    // Verify values
    for (size_t i = 0; i < 1000; ++i) assert(dest[i] == 0xA);
    for (size_t i = 1000; i < 3000; ++i) assert(dest[i] == 0xB);

    std::cout << "test_ring_buffer_wrapping passed." << std::endl;
}

void test_async_log_manager() {
    std::cout << "Running test_async_log_manager..." << std::endl;
    std::string log_file = "test_async_wal.log";
    std::remove(log_file.c_str());

    {
        FenrirDB::AsyncLogManager log_mgr(log_file);
        
        // Append concurrent records from multiple threads
        std::vector<std::thread> threads;
        for (int i = 0; i < 5; ++i) {
            threads.emplace_back([&log_mgr, i]() {
                for (int j = 0; j < 100; ++j) {
                    log_mgr.append_record(i + 1, FenrirDB::LogRecordType::UPDATE, 0, 0, {}, {1, 2, 3});
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        // Flush up to LSN 200
        log_mgr.flush(200);
        assert(log_mgr.get_flushed_lsn() >= 200);
    }

    std::remove(log_file.c_str());
    std::cout << "test_async_log_manager passed." << std::endl;
}

int main() {
    test_ring_buffer_wrapping();
    test_async_log_manager();
    std::cout << "All WAL Buffer tests passed successfully!" << std::endl;
    return 0;
}
