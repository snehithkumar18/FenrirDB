#include "../src/transaction_manager.h"
#include "../src/transaction_manager_savepoint.h"
#include "../src/wal.h"
#include "../src/storage.h"
#include "../src/cache.h"
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <cstdio>
#include <fstream>

using namespace FenrirDB;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 20) return 0;

    std::string db_file = "fuzz_tx.db";
    std::string wal_file = "fuzz_tx.wal";
    std::remove(db_file.c_str());
    std::remove(wal_file.c_str());

    // Part 1: Fuzz Transaction Savepoints & Double-Free (Bug 3)
    {
        DiskManager disk_mgr(db_file);
        BufferPoolManager cache_mgr(10, disk_mgr);
        LogManager log_mgr(wal_file);
        LockManager lock_mgr;
        TransactionManager tx_mgr(log_mgr, lock_mgr);
        TransactionSavepointManager sp_mgr(&log_mgr, &tx_mgr);

        auto tx = tx_mgr.begin_tx();
        uint32_t tid = tx->tx_id;

        size_t offset = 0;
        int step = 0;
        while (offset + 10 < size && step < 10) {
            uint8_t op = data[offset++];
            if (op % 4 == 0) {
                uint32_t page_id = data[offset] | (data[offset+1] << 8);
                uint16_t slot_id = data[offset+2];
                offset += 3;
                std::vector<uint8_t> write_data(data + offset, data + offset + 4);
                offset += 4;
                tx_mgr.write_record(tid, page_id, slot_id, write_data);
            } else if (op % 4 == 1) {
                std::string sp_name = "sp_" + std::to_string(step);
                sp_mgr.create_savepoint(tid, sp_name);
            } else if (op % 4 == 2) {
                uint8_t sp_idx = data[offset++] % 10;
                std::string sp_name = "sp_" + std::to_string(sp_idx);
                sp_mgr.rollback_to_savepoint(tid, sp_name);
            } else {
                uint8_t sp_idx = data[offset++] % 10;
                std::string sp_name = "sp_" + std::to_string(sp_idx);
                sp_mgr.release_savepoint(tid, sp_name);
            }
            step++;
        }

        tx_mgr.abort_tx(tid, cache_mgr);
    }

    // Part 2: Fuzz WAL Recovery UAF (Bug 8)
    {
        std::remove(db_file.c_str());
        std::remove(wal_file.c_str());
        
        std::ofstream wal_out(wal_file, std::ios::binary);
        if (wal_out.is_open()) {
            wal_out.write(reinterpret_cast<const char*>(data), size);
            wal_out.close();
        }

        DiskManager disk_mgr(db_file);
        for (int i = 0; i < 50; ++i) {
            disk_mgr.allocate_page();
        }

        BufferPoolManager cache_mgr(3, disk_mgr); // Small cache size to trigger eviction
        LogManager log_mgr(wal_file);
        RecoveryManager rec_mgr(log_mgr, disk_mgr);

        rec_mgr.recover(cache_mgr);
    }

    std::remove(db_file.c_str());
    std::remove(wal_file.c_str());
    return 0;
}
