#include "../src/storage.h"
#include <iostream>
#include <cassert>
#include <cstring>
#include <cstdio>

void test_page_basic() {
    std::cout << "Running test_page_basic..." << std::endl;
    FenrirDB::Page page(1);
    assert(page.get_page_id() == 1);
    assert(page.get_num_records() == 0);
    assert(page.get_free_space() == FenrirDB::PAGE_SIZE - 8);
    std::cout << "test_page_basic passed." << std::endl;
}

void test_page_records() {
    std::cout << "Running test_page_records..." << std::endl;
    FenrirDB::Page page(2);

    std::string rec1 = "Hello, FenrirDB!";
    std::string rec2 = "Slotted-Page Storage System Testing.";

    // Insert records
    DBErrorCode res = page.insert_record(0, reinterpret_cast<const uint8_t*>(rec1.c_str()), rec1.size());
    assert(res == DBErrorCode::SUCCESS);
    assert(page.get_num_records() == 1);

    res = page.insert_record(1, reinterpret_cast<const uint8_t*>(rec2.c_str()), rec2.size());
    assert(res == DBErrorCode::SUCCESS);
    assert(page.get_num_records() == 2);

    // Fetch and check records
    std::vector<uint8_t> data1;
    res = page.get_record(0, data1);
    assert(res == DBErrorCode::SUCCESS);
    std::string fetched1(data1.begin(), data1.end());
    assert(fetched1 == rec1);

    std::vector<uint8_t> data2;
    res = page.get_record(1, data2);
    assert(res == DBErrorCode::SUCCESS);
    std::string fetched2(data2.begin(), data2.end());
    assert(fetched2 == rec2);

    std::cout << "test_page_records passed." << std::endl;
}

void test_page_compaction() {
    std::cout << "Running test_page_compaction..." << std::endl;
    FenrirDB::Page page(3);

    std::string rec1 = "First Record Data";
    std::string rec2 = "Second Record Data to Delete";
    std::string rec3 = "Third Record Data Remaining";

    page.insert_record(0, reinterpret_cast<const uint8_t*>(rec1.c_str()), rec1.size());
    page.insert_record(1, reinterpret_cast<const uint8_t*>(rec2.c_str()), rec2.size());
    page.insert_record(2, reinterpret_cast<const uint8_t*>(rec3.c_str()), rec3.size());

    // Delete record 1 to create a hole
    DBErrorCode res = page.delete_record(1);
    assert(res == DBErrorCode::SUCCESS);

    // Compacting page to reclaim deleted space
    page.compact();

    // Verify record 0 and 2 are still valid
    std::vector<uint8_t> data0;
    page.get_record(0, data0);
    assert(std::string(data0.begin(), data0.end()) == rec1);

    std::vector<uint8_t> data2;
    page.get_record(2, data2);
    assert(std::string(data2.begin(), data2.end()) == rec3);

    std::cout << "test_page_compaction passed." << std::endl;
}

void test_disk_manager() {
    std::cout << "Running test_disk_manager..." << std::endl;
    std::string db_file = "test_disk.db";
    std::remove(db_file.c_str());

    {
        FenrirDB::DiskManager disk_mgr(db_file);
        uint32_t p0 = disk_mgr.allocate_page();
        uint32_t p1 = disk_mgr.allocate_page();
        assert(p0 == 0);
        assert(p1 == 1);
        assert(disk_mgr.get_num_pages() == 2);

        FenrirDB::Page page;
        page.insert_record(0, reinterpret_cast<const uint8_t*>("Page 0 Data"), 11);
        disk_mgr.write_page(p0, &page);
    }

    // Reopen and check
    {
        FenrirDB::DiskManager disk_mgr(db_file);
        assert(disk_mgr.get_num_pages() == 2);

        FenrirDB::Page page;
        DBErrorCode res = disk_mgr.read_page(0, &page);
        assert(res == DBErrorCode::SUCCESS);

        std::vector<uint8_t> data;
        page.get_record(0, data);
        assert(std::string(data.begin(), data.end()) == "Page 0 Data");
    }

    std::remove(db_file.c_str());
    std::cout << "test_disk_manager passed." << std::endl;
}

int main() {
    test_page_basic();
    test_page_records();
    test_page_compaction();
    test_disk_manager();
    std::cout << "All Storage tests passed successfully!" << std::endl;
    return 0;
}
