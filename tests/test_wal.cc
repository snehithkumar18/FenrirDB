#include "../src/wal.h"
#include "../src/errors.h"
#include "../src/storage.h"
#include "../src/cache.h"
#include <iostream>
#include <cassert>
#include <cstdio>
#include <vector>

using FenrirDB::DBErrorCode;

void test_log_record_serialization() {
    std::cout << "Running test_log_record_serialization..." << std::endl;
    FenrirDB::LogRecord rec;
    rec.lsn = 42;
    rec.tx_id = 9;
    rec.type = FenrirDB::LogRecordType::UPDATE;
    rec.page_id = 3;
    rec.slot_id = 1;
    rec.before_image = { 1, 2, 3 };
    rec.after_image = { 4, 5, 6, 7 };

    std::vector<uint8_t> bytes = rec.serialize();
    size_t offset = 0;
    FenrirDB::LogRecord deserialized = FenrirDB::LogRecord::deserialize(bytes, offset);

    assert(deserialized.lsn == rec.lsn);
    assert(deserialized.tx_id == rec.tx_id);
    assert(deserialized.type == rec.type);
    assert(deserialized.page_id == rec.page_id);
    assert(deserialized.slot_id == rec.slot_id);
    assert(deserialized.before_image == rec.before_image);
    assert(deserialized.after_image == rec.after_image);

    std::cout << "test_log_record_serialization passed." << std::endl;
}

void test_recovery_rollback() {
    std::cout << "Running test_recovery_rollback..." << std::endl;
    std::string db_file = "test_recovery.db";
    std::string log_file = "test_recovery.log";
    std::remove(db_file.c_str());
    std::remove(log_file.c_str());

    {
        FenrirDB::DiskManager disk_mgr(db_file);
        FenrirDB::BufferPoolManager cache_mgr(5, disk_mgr);
        FenrirDB::LogManager log_mgr(log_file);

        uint32_t p0 = disk_mgr.allocate_page();
        FenrirDB::Page* page = cache_mgr.fetch_page(p0);
        new (page) FenrirDB::Page(p0);

        // Transaction 1: Inserts "Alice" (Committed)
        log_mgr.append_record(1, FenrirDB::LogRecordType::BEGIN);
        std::vector<uint8_t> val1 = { 'A', 'l', 'i', 'c', 'e' };
        page->insert_record(0, val1.data(), val1.size());
        log_mgr.append_record(1, FenrirDB::LogRecordType::INSERT, p0, 0, {}, val1);
        log_mgr.append_record(1, FenrirDB::LogRecordType::COMMIT);

        // Transaction 2: Inserts "Bob" (Uncommitted / Aborted)
        log_mgr.append_record(2, FenrirDB::LogRecordType::BEGIN);
        std::vector<uint8_t> val2 = { 'B', 'o', 'b' };
        page->insert_record(1, val2.data(), val2.size());
        log_mgr.append_record(2, FenrirDB::LogRecordType::INSERT, p0, 1, {}, val2);
        // Crash happens here (no COMMIT log record for Tx 2)

        cache_mgr.flush_all();
    }

    // Reopen and recover
    {
        FenrirDB::DiskManager disk_mgr(db_file);
        FenrirDB::BufferPoolManager cache_mgr(5, disk_mgr);
        FenrirDB::LogManager log_mgr(log_file);
        FenrirDB::RecoveryManager rm(log_mgr, disk_mgr);

        // Recovery pass: REDO Tx 1, UNDO/Rollback Tx 2
        DBErrorCode res = rm.recover(cache_mgr);
        assert(res == DBErrorCode::SUCCESS);

        FenrirDB::Page* page = cache_mgr.fetch_page(0);
        assert(page != nullptr);

        // Verify "Alice" is still present
        std::vector<uint8_t> res1;
        assert(page->get_record(0, res1) == DBErrorCode::SUCCESS);
        assert(std::string(res1.begin(), res1.end()) == "Alice");

        // Verify "Bob" was rolled back (slot should be empty)
        std::vector<uint8_t> res2;
        assert(page->get_record(1, res2) == DBErrorCode::ERR_RECORD_NOT_FOUND);
    }

    std::remove(db_file.c_str());
    std::remove(log_file.c_str());
    std::cout << "test_recovery_rollback passed." << std::endl;
}

int main() {
    test_log_record_serialization();
    test_recovery_rollback();
    std::cout << "All WAL recovery tests passed successfully!" << std::endl;
    return 0;
}
