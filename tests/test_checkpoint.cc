#include "../src/checkpoint.h"
#include <iostream>
#include <cassert>
#include <cstdio>

void test_checkpoint_serialization() {
    std::cout << "Running test_checkpoint_serialization..." << std::endl;
    FenrirDB::CheckpointData data;
    data.dirty_page_table[1] = 100;
    data.dirty_page_table[5] = 250;

    data.active_tx_table[10] = 50;
    data.active_tx_table[12] = 80;

    std::vector<uint8_t> bytes = data.serialize();
    FenrirDB::CheckpointData deserialized = FenrirDB::CheckpointData::deserialize(bytes);

    assert(deserialized.dirty_page_table.size() == 2);
    assert(deserialized.dirty_page_table[1] == 100);
    assert(deserialized.dirty_page_table[5] == 250);

    assert(deserialized.active_tx_table.size() == 2);
    assert(deserialized.active_tx_table[10] == 50);
    assert(deserialized.active_tx_table[12] == 80);

    std::cout << "test_checkpoint_serialization passed." << std::endl;
}

void test_checkpoint_manager() {
    std::cout << "Running test_checkpoint_manager..." << std::endl;
    std::string db_file = "test_cp.db";
    std::remove(db_file.c_str());

    FenrirDB::DiskManager disk_mgr(db_file);
    FenrirDB::BufferPoolManager cache_mgr(10, disk_mgr);
    FenrirDB::LogManager log_mgr(db_file + ".wal");
    FenrirDB::LockManager lock_mgr;
    FenrirDB::TransactionManager tx_mgr(log_mgr, lock_mgr);

    FenrirDB::CheckpointManager cp_mgr(log_mgr, cache_mgr, tx_mgr);

    // Active updates: mark some pages dirty
    cp_mgr.mark_page_dirty(1, 100);
    cp_mgr.mark_page_dirty(2, 120);

    // Begin checkpoint
    uint64_t safe_lsn = cp_mgr.begin_checkpoint();
    assert(safe_lsn > 0);

    std::remove(db_file.c_str());
    std::remove((db_file + ".wal").c_str());
    std::cout << "test_checkpoint_manager passed." << std::endl;
}

int main() {
    test_checkpoint_serialization();
    test_checkpoint_manager();
    std::cout << "All Checkpoint tests passed successfully!" << std::endl;
    return 0;
}
