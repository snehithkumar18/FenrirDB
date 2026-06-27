#include "../src/index.h"
#include <iostream>
#include <cassert>
#include <cstdio>

void test_index_basic() {
    std::cout << "Running test_index_basic..." << std::endl;
    std::string db_file = "test_index.db";
    std::remove(db_file.c_str());

    FenrirDB::DiskManager disk_mgr(db_file);
    FenrirDB::BufferPoolManager cache_mgr(5, disk_mgr);

    // Page 0 is root page
    uint32_t root_id = disk_mgr.allocate_page();
    FenrirDB::Page* root_page = cache_mgr.fetch_page(root_id);
    if (root_page) {
        FenrirDB::IndexNode* node = reinterpret_cast<FenrirDB::IndexNode*>(root_page->data + 8);
        new (node) FenrirDB::IndexNode();
        node->is_leaf = true;
        node->num_keys = 0;
        cache_mgr.flush_page(root_id);
    }

    FenrirDB::BPlusTreeIndex idx(disk_mgr, cache_mgr, root_id);

    // Insert keys
    FenrirDB::RecordID r1 = { 10, 1 };
    FenrirDB::RecordID r2 = { 11, 2 };
    FenrirDB::RecordID r3 = { 12, 3 };

    DBErrorCode res = idx.insert(FenrirDB::CompositeKey("Alice"), r1);
    assert(res == DBErrorCode::SUCCESS);
    
    res = idx.insert(FenrirDB::CompositeKey("Charlie"), r3);
    assert(res == DBErrorCode::SUCCESS);

    res = idx.insert(FenrirDB::CompositeKey("Bob"), r2);
    assert(res == DBErrorCode::SUCCESS);

    // Search keys
    FenrirDB::RecordID val;
    res = idx.search(FenrirDB::CompositeKey("Bob"), val);
    assert(res == DBErrorCode::SUCCESS);
    assert(val == r2);

    res = idx.search(FenrirDB::CompositeKey("Alice"), val);
    assert(res == DBErrorCode::SUCCESS);
    assert(val == r1);

    res = idx.search(FenrirDB::CompositeKey("Charlie"), val);
    assert(res == DBErrorCode::SUCCESS);
    assert(val == r3);

    std::remove(db_file.c_str());
    std::cout << "test_index_basic passed." << std::endl;
}

void test_index_range_search() {
    std::cout << "Running test_index_range_search..." << std::endl;
    std::string db_file = "test_index_range.db";
    std::remove(db_file.c_str());

    FenrirDB::DiskManager disk_mgr(db_file);
    FenrirDB::BufferPoolManager cache_mgr(5, disk_mgr);

    uint32_t root_id = disk_mgr.allocate_page();
    FenrirDB::Page* root_page = cache_mgr.fetch_page(root_id);
    if (root_page) {
        FenrirDB::IndexNode* node = reinterpret_cast<FenrirDB::IndexNode*>(root_page->data + 8);
        new (node) FenrirDB::IndexNode();
        node->is_leaf = true;
        node->num_keys = 0;
        cache_mgr.flush_page(root_id);
    }

    FenrirDB::BPlusTreeIndex idx(disk_mgr, cache_mgr, root_id);

    idx.insert(FenrirDB::CompositeKey("key10"), { 10, 0 });
    idx.insert(FenrirDB::CompositeKey("key20"), { 20, 0 });
    idx.insert(FenrirDB::CompositeKey("key30"), { 30, 0 });
    idx.insert(FenrirDB::CompositeKey("key40"), { 40, 0 });

    std::vector<FenrirDB::RecordID> results;
    DBErrorCode res = idx.range_search(FenrirDB::CompositeKey("key20"), FenrirDB::CompositeKey("key35"), results);
    assert(res == DBErrorCode::SUCCESS);
    assert(results.size() == 2);
    assert(results[0].page_id == 20);
    assert(results[1].page_id == 30);

    std::remove(db_file.c_str());
    std::cout << "test_index_range_search passed." << std::endl;
}

int main() {
    test_index_basic();
    test_index_range_search();
    std::cout << "All Index tests passed successfully!" << std::endl;
    return 0;
}
