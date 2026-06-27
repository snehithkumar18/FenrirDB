#include "../src/transaction_manager.h"
#include "../src/database.h"
#include <iostream>
#include <cassert>
#include <cstdio>

void test_transaction_commit_visibility() {
    std::cout << "Running test_transaction_commit_visibility..." << std::endl;
    std::string db_file = "test_tx_vis.db";
    std::remove(db_file.c_str());

    FenrirDB::DiskManager disk_mgr(db_file);
    FenrirDB::BufferPoolManager cache_mgr(10, disk_mgr);
    FenrirDB::LogManager log_mgr(db_file + ".wal");
    FenrirDB::LockManager lock_mgr;
    FenrirDB::TransactionManager tx_mgr(log_mgr, lock_mgr);

    // Start Transaction 1
    auto tx1 = tx_mgr.begin_tx();
    uint32_t tid1 = tx1->tx_id;

    // Start Transaction 2
    auto tx2 = tx_mgr.begin_tx();
    uint32_t tid2 = tx2->tx_id;

    // Transaction 1 inserts a record with LSN = 10
    uint64_t record_lsn = 10;
    
    // According to MVCC, Tx 2 started before Tx 1 committed/inserted at LSN 10,
    // so record_lsn 10 should NOT be visible to Tx 2 (snapshot isolation)
    assert(tx_mgr.is_visible(tid2, record_lsn) == false);

    // Commit Transaction 1
    tx_mgr.commit_tx(tid1);

    // Start Transaction 3 (after Tx 1 committed)
    auto tx3 = tx_mgr.begin_tx();
    uint32_t tid3 = tx3->tx_id;

    // The record LSN 10 should be visible to Transaction 3
    assert(tx_mgr.is_visible(tid3, record_lsn) == true);

    tx_mgr.commit_tx(tid2);
    tx_mgr.commit_tx(tid3);

    std::remove(db_file.c_str());
    std::remove((db_file + ".wal").c_str());
    std::cout << "test_transaction_commit_visibility passed." << std::endl;
}

int main() {
    test_transaction_commit_visibility();
    std::cout << "All Transaction Manager tests passed successfully!" << std::endl;
    return 0;
}
