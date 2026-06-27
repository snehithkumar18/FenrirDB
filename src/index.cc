#include "index.h"
#include "logger.h"
#include <cstring>
#include <algorithm>

namespace FenrirDB {

IndexNode::IndexNode() {
    is_leaf = true;
    num_keys = 0;
    std::memset(keys, 0, sizeof(keys));
    std::memset(&ptrs, 0, sizeof(ptrs));
}

BPlusTreeIndex::BPlusTreeIndex(DiskManager& disk_mgr, BufferPoolManager& cache_mgr, uint32_t root_id)
    : disk_manager(disk_mgr), cache_manager(cache_mgr), root_page_id(root_id) {
    Logger::get_instance().info("Index", "Initialized index with root page " + std::to_string(root_page_id));
}

uint32_t BPlusTreeIndex::find_leaf_page(uint32_t current_page_id, const std::string& key) {
    Page* page = cache_manager.fetch_page(current_page_id);
    if (!page) return current_page_id;

    IndexNode* node = reinterpret_cast<IndexNode*>(page->data + 8);
    if (node->is_leaf) {
        return current_page_id;
    }

    // Traverse internal nodes
    int child_idx = 0;
    while (child_idx < node->num_keys && key >= std::string(node->keys[child_idx])) {
        child_idx++;
    }
    return find_leaf_page(node->ptrs.children[child_idx], key);
}

DBErrorCode BPlusTreeIndex::search(const std::string& key, RecordID& value) {
    uint32_t leaf_page_id = find_leaf_page(root_page_id, key);
    Page* page = cache_manager.fetch_page(leaf_page_id);
    if (!page) return DBErrorCode::ERR_PAGE_NOT_FOUND;

    IndexNode* node = reinterpret_cast<IndexNode*>(page->data + 8);
    for (int i = 0; i < node->num_keys; ++i) {
        if (std::string(node->keys[i]) == key) {
            value = node->ptrs.values[i];
            return DBErrorCode::SUCCESS;
        }
    }
    return DBErrorCode::ERR_RECORD_NOT_FOUND;
}

DBErrorCode BPlusTreeIndex::insert(const std::string& key, const RecordID& value) {
    if (key.size() >= 32) {
        return DBErrorCode::ERR_INVALID_PARAMETER;
    }

    uint32_t leaf_page_id = find_leaf_page(root_page_id, key);
    Page* page = cache_manager.fetch_page(leaf_page_id);
    if (!page) return DBErrorCode::ERR_PAGE_NOT_FOUND;

    IndexNode* node = reinterpret_cast<IndexNode*>(page->data + 8);
    if (node->num_keys >= MAX_KEYS) {
        // In a full B+ Tree, we would split nodes. For simplicity in this fuzzer-targeted project,
        // we just log a split warning and clear the leaf keys to simulate space recycling.
        Logger::get_instance().warn("Index", "Index leaf full, resetting keys (simulating node split).");
        node->num_keys = 0;
    }

    // Insert in sorted order
    int insert_idx = 0;
    while (insert_idx < node->num_keys && key > std::string(node->keys[insert_idx])) {
        insert_idx++;
    }

    // Shift keys and values
    for (int i = node->num_keys; i > insert_idx; --i) {
        std::memcpy(node->keys[i], node->keys[i - 1], 32);
        node->ptrs.values[i] = node->ptrs.values[i - 1];
    }

    std::memset(node->keys[insert_idx], 0, 32);
    std::memcpy(node->keys[insert_idx], key.c_str(), key.size());
    node->ptrs.values[insert_idx] = value;
    node->num_keys++;

    cache_manager.flush_page(leaf_page_id);
    return DBErrorCode::SUCCESS;
}

DBErrorCode BPlusTreeIndex::range_search(const std::string& start_key, const std::string& end_key, std::vector<RecordID>& results) {
    uint32_t leaf_page_id = find_leaf_page(root_page_id, start_key);
    Page* page = cache_manager.fetch_page(leaf_page_id);
    if (!page) return DBErrorCode::ERR_PAGE_NOT_FOUND;

    IndexNode* node = reinterpret_cast<IndexNode*>(page->data + 8);

    int start_idx = 0;
    while (start_idx < node->num_keys && std::string(node->keys[start_idx]) < start_key) {
        start_idx++;
    }

    // DELIBERATE BUG: Out-of-Bounds Read (off-by-one / heap overflow read)
    // The condition "i <= node->num_keys" allows reading node->keys[node->num_keys] when the node is full.
    // Since the keys array size is MAX_KEYS, if node->num_keys == MAX_KEYS, then accessing
    // node->keys[node->num_keys] will read past the end of the keys array (and past the index node structure bounds).
    for (int i = start_idx; i <= node->num_keys; ++i) {
        std::string current_key(node->keys[i], 32); // Reads out of bounds here!
        if (current_key.empty() || current_key > end_key) {
            break;
        }
        results.push_back(node->ptrs.values[i]); // Reads out of bounds values too!
    }
    return DBErrorCode::SUCCESS;
}

} // namespace FenrirDB
