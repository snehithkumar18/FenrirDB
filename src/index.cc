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

uint32_t BPlusTreeIndex::find_leaf_page(uint32_t current_page_id, const std::string& key, std::vector<uint32_t>* path) {
    if (path) {
        path->push_back(current_page_id);
    }
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
    return find_leaf_page(node->ptrs.children[child_idx], key, path);
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

    std::vector<uint32_t> path;
    uint32_t leaf_page_id = find_leaf_page(root_page_id, key, &path);
    Page* page = cache_manager.fetch_page(leaf_page_id);
    if (!page) return DBErrorCode::ERR_PAGE_NOT_FOUND;

    IndexNode* node = reinterpret_cast<IndexNode*>(page->data + 8);
    if (node->num_keys < MAX_KEYS) {
        // Insert key in sorted order
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

    // Leaf is full: split leaf!
    split_leaf(leaf_page_id, key, value, path);
    return DBErrorCode::SUCCESS;
}

void BPlusTreeIndex::split_leaf(uint32_t leaf_id, const std::string& key, const RecordID& value, std::vector<uint32_t>& path) {
    uint32_t new_leaf_id = disk_manager.allocate_page();
    Page* new_page = cache_manager.fetch_page(new_leaf_id);
    Page* old_page = cache_manager.fetch_page(leaf_id);

    IndexNode* old_node = reinterpret_cast<IndexNode*>(old_page->data + 8);
    IndexNode* new_node = reinterpret_cast<IndexNode*>(new_page->data + 8);
    new (new_node) IndexNode();
    new_node->is_leaf = true;

    // Collect and sort all keys/values including the new insert
    std::vector<std::pair<std::string, RecordID>> temp;
    for (int i = 0; i < old_node->num_keys; ++i) {
        temp.push_back({std::string(old_node->keys[i]), old_node->ptrs.values[i]});
    }
    temp.push_back({key, value});
    std::sort(temp.begin(), temp.end());

    int split_idx = temp.size() / 2;

    // Repopulate old node
    old_node->num_keys = 0;
    std::memset(old_node->keys, 0, sizeof(old_node->keys));
    for (int i = 0; i < split_idx; ++i) {
        std::memcpy(old_node->keys[i], temp[i].first.c_str(), temp[i].first.size());
        old_node->ptrs.values[i] = temp[i].second;
        old_node->num_keys++;
    }

    // Populate new node
    new_node->num_keys = 0;
    for (size_t i = split_idx; i < temp.size(); ++i) {
        std::memcpy(new_node->keys[new_node->num_keys], temp[i].first.c_str(), temp[i].first.size());
        new_node->ptrs.values[new_node->num_keys] = temp[i].second;
        new_node->num_keys++;
    }

    cache_manager.flush_page(leaf_id);
    cache_manager.flush_page(new_leaf_id);

    std::string promote_key = temp[split_idx].first;
    insert_into_parent(leaf_id, promote_key, new_leaf_id, path);
}

void BPlusTreeIndex::insert_into_parent(uint32_t left_id, const std::string& key, uint32_t right_id, std::vector<uint32_t>& path) {
    if (left_id == root_page_id) {
        // Root split: create new root node
        uint32_t new_root_id = disk_manager.allocate_page();
        Page* new_root_page = cache_manager.fetch_page(new_root_id);

        IndexNode* new_root = reinterpret_cast<IndexNode*>(new_root_page->data + 8);
        new (new_root) IndexNode();
        new_root->is_leaf = false;
        std::memcpy(new_root->keys[0], key.c_str(), key.size());
        new_root->ptrs.children[0] = left_id;
        new_root->ptrs.children[1] = right_id;
        new_root->num_keys = 1;

        root_page_id = new_root_id;
        cache_manager.flush_page(new_root_id);
        Logger::get_instance().info("Index", "Split completed. Created new root page: " + std::to_string(root_page_id));
        return;
    }

    path.pop_back(); // Remove current leaf_id
    uint32_t parent_id = path.back();
    Page* parent_page = cache_manager.fetch_page(parent_id);
    IndexNode* parent_node = reinterpret_cast<IndexNode*>(parent_page->data + 8);

    if (parent_node->num_keys < MAX_KEYS) {
        // Simple insert into internal parent node
        int insert_idx = 0;
        while (insert_idx < parent_node->num_keys && key > std::string(parent_node->keys[insert_idx])) {
            insert_idx++;
        }
        for (int i = parent_node->num_keys; i > insert_idx; --i) {
            std::memcpy(parent_node->keys[i], parent_node->keys[i - 1], 32);
            parent_node->ptrs.children[i + 1] = parent_node->ptrs.children[i];
        }
        std::memset(parent_node->keys[insert_idx], 0, 32);
        std::memcpy(parent_node->keys[insert_idx], key.c_str(), key.size());
        parent_node->ptrs.children[insert_idx + 1] = right_id;
        parent_node->num_keys++;
        cache_manager.flush_page(parent_id);
    } else {
        // Parent internal node is full: split parent recursively!
        split_internal(parent_id, key, right_id, path);
    }
}

void BPlusTreeIndex::split_internal(uint32_t parent_id, const std::string& key, uint32_t child_id, std::vector<uint32_t>& path) {
    uint32_t new_parent_id = disk_manager.allocate_page();
    Page* new_parent_page = cache_manager.fetch_page(new_parent_id);
    Page* old_parent_page = cache_manager.fetch_page(parent_id);

    IndexNode* old_parent = reinterpret_cast<IndexNode*>(old_parent_page->data + 8);
    IndexNode* new_parent = reinterpret_cast<IndexNode*>(new_parent_page->data + 8);
    new (new_parent) IndexNode();
    new_parent->is_leaf = false;

    std::vector<std::string> temp_keys;
    std::vector<uint32_t> temp_children;

    for (int i = 0; i < old_parent->num_keys; ++i) {
        temp_keys.push_back(std::string(old_parent->keys[i]));
    }
    for (int i = 0; i <= old_parent->num_keys; ++i) {
        temp_children.push_back(old_parent->ptrs.children[i]);
    }

    int insert_idx = 0;
    while (insert_idx < static_cast<int>(temp_keys.size()) && key > temp_keys[insert_idx]) {
        insert_idx++;
    }
    temp_keys.insert(temp_keys.begin() + insert_idx, key);
    temp_children.insert(temp_children.begin() + insert_idx + 1, child_id);

    int split_idx = temp_keys.size() / 2;
    std::string promote_key = temp_keys[split_idx];

    // Repopulate old parent
    old_parent->num_keys = 0;
    std::memset(old_parent->keys, 0, sizeof(old_parent->keys));
    for (int i = 0; i < split_idx; ++i) {
        std::memcpy(old_parent->keys[i], temp_keys[i].c_str(), temp_keys[i].size());
        old_parent->ptrs.children[i] = temp_children[i];
        old_parent->num_keys++;
    }
    old_parent->ptrs.children[split_idx] = temp_children[split_idx];

    // Populate new parent
    new_parent->num_keys = 0;
    for (size_t i = split_idx + 1; i < temp_keys.size(); ++i) {
        std::memcpy(new_parent->keys[new_parent->num_keys], temp_keys[i].c_str(), temp_keys[i].size());
        new_parent->ptrs.children[new_parent->num_keys] = temp_children[i];
        new_parent->num_keys++;
    }
    new_parent->ptrs.children[new_parent->num_keys] = temp_children.back();

    cache_manager.flush_page(parent_id);
    cache_manager.flush_page(new_parent_id);

    insert_into_parent(parent_id, promote_key, new_parent_id, path);
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
