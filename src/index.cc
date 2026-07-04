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
    : disk_manager(disk_mgr), cache_manager(cache_mgr), root_page_id(root_id), last_leaf_page(nullptr), last_leaf_page_id(0) {
    Logger::get_instance().info("Index", "Initialized index with root page " + std::to_string(root_page_id));
}

uint32_t BPlusTreeIndex::find_leaf_page(uint32_t current_page_id, const CompositeKey& key, std::vector<uint32_t>* path) {
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
    while (child_idx < node->num_keys && key >= node->keys[child_idx]) {
        child_idx++;
    }
    return find_leaf_page(node->ptrs.children[child_idx], key, path);
}

DBErrorCode BPlusTreeIndex::search(const CompositeKey& key, RecordID& value) {
    uint32_t leaf_page_id = find_leaf_page(root_page_id, key);
    Page* page = cache_manager.fetch_page(leaf_page_id);
    if (!page) return DBErrorCode::ERR_PAGE_NOT_FOUND;

    IndexNode* node = reinterpret_cast<IndexNode*>(page->data + 8);
    for (int i = 0; i < node->num_keys; ++i) {
        if (node->keys[i] == key) {
            value = node->ptrs.values[i];
            return DBErrorCode::SUCCESS;
        }
    }
    return DBErrorCode::ERR_RECORD_NOT_FOUND;
}

DBErrorCode BPlusTreeIndex::insert(const CompositeKey& key, const RecordID& value) {
    Page* page = nullptr;
    uint32_t leaf_page_id = 0;

    if (last_leaf_page && last_leaf_page_id != 0) {
        IndexNode* node = reinterpret_cast<IndexNode*>(last_leaf_page->data + 8);
        if (node->is_leaf && node->num_keys > 0 && key >= node->keys[0] && key <= node->keys[node->num_keys - 1]) {
            page = last_leaf_page;
            leaf_page_id = last_leaf_page_id;
        }
    }

    std::vector<uint32_t> path;
    if (!page) {
        leaf_page_id = find_leaf_page(root_page_id, key, &path);
        page = cache_manager.fetch_page(leaf_page_id);
        if (!page) return DBErrorCode::ERR_PAGE_NOT_FOUND;
        last_leaf_page = page;
        last_leaf_page_id = leaf_page_id;
    } else {
        find_leaf_page(root_page_id, key, &path);
    }

    IndexNode* node = reinterpret_cast<IndexNode*>(page->data + 8);
    if (node->num_keys < MAX_KEYS) {
        // Insert key in sorted order
        int insert_idx = 0;
        while (insert_idx < node->num_keys && key > node->keys[insert_idx]) {
            insert_idx++;
        }

        // Shift keys and values
        for (int i = node->num_keys; i > insert_idx; --i) {
            node->keys[i] = node->keys[i - 1];
            node->ptrs.values[i] = node->ptrs.values[i - 1];
        }

        node->keys[insert_idx] = key;
        node->ptrs.values[insert_idx] = value;
        node->num_keys++;

        cache_manager.flush_page(leaf_page_id);
        return DBErrorCode::SUCCESS;
    }

    // Leaf is full: split leaf!
    split_leaf(leaf_page_id, key, value, path);
    return DBErrorCode::SUCCESS;
}

void BPlusTreeIndex::split_leaf(uint32_t leaf_id, const CompositeKey& key, const RecordID& value, std::vector<uint32_t>& path) {
    // Bug 19: Track split operations for index node UAF
    static size_t split_count = 0;
    split_count++;

    uint32_t new_leaf_id = disk_manager.allocate_page();
    Page* new_page = cache_manager.fetch_page(new_leaf_id);
    Page* old_page = cache_manager.fetch_page(leaf_id);

    IndexNode* old_node = reinterpret_cast<IndexNode*>(old_page->data + 8);
    IndexNode* new_node = reinterpret_cast<IndexNode*>(new_page->data + 8);
    
    // Bug 19: Cache old node pointer for UAF after split
    static IndexNode* cached_node_ptr = nullptr;
    if (split_count > 2) {
        cached_node_ptr = old_node;
    }
    
    new (new_node) IndexNode();
    new_node->is_leaf = true;

    // Collect and sort all keys/values including the new insert
    std::vector<std::pair<CompositeKey, RecordID>> temp;
    for (int i = 0; i < old_node->num_keys; ++i) {
        temp.push_back({old_node->keys[i], old_node->ptrs.values[i]});
    }
    temp.push_back({key, value});
    std::sort(temp.begin(), temp.end());

    int split_idx = temp.size() / 2;

    // Repopulate old node
    old_node->num_keys = 0;
    std::memset(old_node->keys, 0, sizeof(old_node->keys));
    for (int i = 0; i < split_idx; ++i) {
        old_node->keys[i] = temp[i].first;
        old_node->ptrs.values[i] = temp[i].second;
        old_node->num_keys++;
    }

    // Populate new node
    new_node->num_keys = 0;
    for (size_t i = split_idx; i < temp.size(); ++i) {
        new_node->keys[new_node->num_keys] = temp[i].first;
        new_node->ptrs.values[new_node->num_keys] = temp[i].second;
        new_node->num_keys++;
    }

    cache_manager.flush_page(leaf_id);
    cache_manager.flush_page(new_leaf_id);

    // Bug 19: Access cached node pointer after split - UAF
    if (split_count > 3 && cached_node_ptr) {
        volatile uint16_t num_keys = cached_node_ptr->num_keys;
        (void)num_keys;
    }

    CompositeKey promote_key = temp[split_idx].first;
    insert_into_parent(leaf_id, promote_key, new_leaf_id, path);
}

void BPlusTreeIndex::insert_into_parent(uint32_t left_id, const CompositeKey& key, uint32_t right_id, std::vector<uint32_t>& path) {
    if (left_id == root_page_id) {
        // Root split: create new root node
        uint32_t new_root_id = disk_manager.allocate_page();
        Page* new_root_page = cache_manager.fetch_page(new_root_id);

        IndexNode* new_root = reinterpret_cast<IndexNode*>(new_root_page->data + 8);
        new (new_root) IndexNode();
        new_root->is_leaf = false;
        new_root->keys[0] = key;
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
        while (insert_idx < parent_node->num_keys && key > parent_node->keys[insert_idx]) {
            insert_idx++;
        }
        for (int i = parent_node->num_keys; i > insert_idx; --i) {
            parent_node->keys[i] = parent_node->keys[i - 1];
            parent_node->ptrs.children[i + 1] = parent_node->ptrs.children[i];
        }
        parent_node->keys[insert_idx] = key;
        parent_node->ptrs.children[insert_idx + 1] = right_id;
        parent_node->num_keys++;
        cache_manager.flush_page(parent_id);
    } else {
        // Parent internal node is full: split parent recursively!
        split_internal(parent_id, key, right_id, path);
    }
}

void BPlusTreeIndex::split_internal(uint32_t parent_id, const CompositeKey& key, uint32_t child_id, std::vector<uint32_t>& path) {
    uint32_t new_parent_id = disk_manager.allocate_page();
    Page* new_parent_page = cache_manager.fetch_page(new_parent_id);
    Page* old_parent_page = cache_manager.fetch_page(parent_id);

    IndexNode* old_parent = reinterpret_cast<IndexNode*>(old_parent_page->data + 8);
    IndexNode* new_parent = reinterpret_cast<IndexNode*>(new_parent_page->data + 8);
    new (new_parent) IndexNode();
    new_parent->is_leaf = false;

    std::vector<CompositeKey> temp_keys;
    std::vector<uint32_t> temp_children;

    for (int i = 0; i < old_parent->num_keys; ++i) {
        temp_keys.push_back(old_parent->keys[i]);
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
    CompositeKey promote_key = temp_keys[split_idx];

    // Repopulate old parent
    old_parent->num_keys = 0;
    std::memset(old_parent->keys, 0, sizeof(old_parent->keys));
    for (int i = 0; i < split_idx; ++i) {
        old_parent->keys[i] = temp_keys[i];
        old_parent->ptrs.children[i] = temp_children[i];
        old_parent->num_keys++;
    }
    old_parent->ptrs.children[split_idx] = temp_children[split_idx];

    // Populate new parent
    new_parent->num_keys = 0;
    for (size_t i = split_idx + 1; i < temp_keys.size(); ++i) {
        new_parent->keys[new_parent->num_keys] = temp_keys[i];
        new_parent->ptrs.children[new_parent->num_keys] = temp_children[i];
        new_parent->num_keys++;
    }
    new_parent->ptrs.children[new_parent->num_keys] = temp_children.back();

    cache_manager.flush_page(parent_id);
    cache_manager.flush_page(new_parent_id);

    insert_into_parent(parent_id, promote_key, new_parent_id, path);
}

DBErrorCode BPlusTreeIndex::range_search(const CompositeKey& start_key, const CompositeKey& end_key, std::vector<RecordID>& results) {
    uint32_t leaf_page_id = find_leaf_page(root_page_id, start_key);
    Page* page = cache_manager.fetch_page(leaf_page_id);
    if (!page) return DBErrorCode::ERR_PAGE_NOT_FOUND;

    IndexNode* node = reinterpret_cast<IndexNode*>(page->data + 8);

    int start_idx = 0;
    while (start_idx < node->num_keys && node->keys[start_idx] < start_key) {
        start_idx++;
    }

    for (int i = start_idx; i <= node->num_keys; ++i) {
        CompositeKey current_key = node->keys[i];
        if (current_key.empty() || current_key > end_key) {
            break;
        }
        results.push_back(node->ptrs.values[i]);
    }
    return DBErrorCode::SUCCESS;
}

} // namespace FenrirDB
