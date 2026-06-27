#include "../src/index.h"
#include <iostream>
#include <cassert>
#include <cstdio>
#include <vector>

void test_composite_key_comparisons() {
    std::cout << "Running test_composite_key_comparisons..." << std::endl;
    
    FenrirDB::CompositeKey k1("Alice", "HR");
    FenrirDB::CompositeKey k2("Alice", "IT");
    FenrirDB::CompositeKey k3("Bob", "HR");

    assert(k1 < k2);
    assert(k2 < k3);
    assert(k1 != k2);
    assert(k1 == FenrirDB::CompositeKey("Alice", "HR"));

    std::cout << "test_composite_key_comparisons passed." << std::endl;
}

void test_composite_indexing_basic() {
    std::cout << "Running test_composite_indexing_basic..." << std::endl;
    std::string db_file = "test_comp_idx.db";
    std::remove(db_file.c_str());

    FenrirDB::DiskManager disk_mgr(db_file);
    FenrirDB::BufferPoolManager cache_mgr(10, disk_mgr);

    uint32_t root_id = disk_mgr.allocate_page();
    FenrirDB::Page* root_page = cache_mgr.fetch_page(root_id);
    if (root_page) {
        FenrirDB::IndexNode* node = reinterpret_cast<FenrirDB::IndexNode*>(root_page->data + 8);
        new (node) FenrirDB::IndexNode();
        node->is_leaf = true;
        cache_mgr.flush_page(root_id);
    }

    FenrirDB::BPlusTreeIndex idx(disk_mgr, cache_mgr, root_id);

    // Insert keys
    FenrirDB::RecordID r1 = { 10, 1 };
    FenrirDB::RecordID r2 = { 10, 2 };
    FenrirDB::RecordID r3 = { 20, 1 };

    idx.insert(FenrirDB::CompositeKey("Alice", "HR"), r1);
    idx.insert(FenrirDB::CompositeKey("Alice", "IT"), r2);
    idx.insert(FenrirDB::CompositeKey("Bob", "HR"), r3);

    // Search keys
    FenrirDB::RecordID val;
    assert(idx.search(FenrirDB::CompositeKey("Alice", "IT"), val) == DBErrorCode::SUCCESS);
    assert(val == r2);

    assert(idx.search(FenrirDB::CompositeKey("Alice", "HR"), val) == DBErrorCode::SUCCESS);
    assert(val == r1);

    // Range query search
    std::vector<FenrirDB::RecordID> results;
    DBErrorCode res = idx.range_search(FenrirDB::CompositeKey("Alice", "HR"), FenrirDB::CompositeKey("Alice", "ZZ"), results);
    assert(res == DBErrorCode::SUCCESS);
    assert(results.size() == 2);
    assert(results[0] == r1);
    assert(results[1] == r2);

    std::remove(db_file.c_str());
    std::cout << "test_composite_indexing_basic passed." << std::endl;
}

int main() {
    test_composite_key_comparisons();
    test_composite_indexing_basic();
    std::cout << "All Composite Indexing tests passed successfully!" << std::endl;
    return 0;
}
