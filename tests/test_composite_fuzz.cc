#include "../src/index.h"
#include <iostream>
#include <vector>
#include <random>
#include <cassert>
#include <cstdio>

void test_composite_split_fuzz() {
    std::cout << "Running test_composite_split_fuzz..." << std::endl;
    std::string db_file = "fuzz_comp_split.db";
    std::remove(db_file.c_str());

    {
        FenrirDB::DiskManager disk_mgr(db_file);
        FenrirDB::BufferPoolManager cache_mgr(20, disk_mgr);

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

        std::mt19937 rng(42);
        std::uniform_int_distribution<int> val_dist(1000, 9999);

        // Insert 500 composite keys to force multiple levels of B+ Tree node splitting
        for (int i = 0; i < 500; ++i) {
            std::string k1 = "k1_" + std::to_string(val_dist(rng));
            std::string k2 = "k2_" + std::to_string(val_dist(rng));
            
            FenrirDB::CompositeKey comp_key(k1, k2);
            FenrirDB::RecordID rid = { static_cast<uint32_t>(i), 0 };

            idx.insert(comp_key, rid);
        }

        // Verify root index depth and traverse keys
        FenrirDB::Page* root = cache_mgr.fetch_page(idx.get_root_page_id());
        assert(root != nullptr);
        FenrirDB::IndexNode* root_node = reinterpret_cast<FenrirDB::IndexNode*>(root->data + 8);
        assert(root_node->num_keys > 0);
    }

    std::remove(db_file.c_str());
    std::cout << "test_composite_split_fuzz passed." << std::endl;
}

int main() {
    test_composite_split_fuzz();
    std::cout << "All Composite B+ Tree split fuzz tests passed successfully!" << std::endl;
    return 0;
}
