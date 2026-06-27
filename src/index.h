#ifndef FENRIRDB_INDEX_H
#define FENRIRDB_INDEX_H

#include <string>
#include <vector>
#include "storage.h"
#include "cache.h"
#include "errors.h"

namespace FenrirDB {

constexpr int MAX_KEYS = 16;

struct RecordID {
    uint32_t page_id;
    uint16_t slot_id;

    bool operator==(const RecordID& other) const {
        return page_id == other.page_id && slot_id == other.slot_id;
    }
};

struct CompositeKey {
    char key1[32] = {0};
    char key2[32] = {0};

    CompositeKey() {
        std::memset(key1, 0, 32);
        std::memset(key2, 0, 32);
    }
    CompositeKey(const std::string& k1, const std::string& k2 = "") {
        std::memset(key1, 0, 32);
        std::memset(key2, 0, 32);
        std::memcpy(key1, k1.c_str(), std::min(k1.size(), size_t(31)));
        std::memcpy(key2, k2.c_str(), std::min(k2.size(), size_t(31)));
    }

    bool operator==(const CompositeKey& other) const {
        return std::strcmp(key1, other.key1) == 0 && std::strcmp(key2, other.key2) == 0;
    }
    bool operator!=(const CompositeKey& other) const {
        return !(*this == other);
    }
    bool operator<(const CompositeKey& other) const {
        int cmp = std::strcmp(key1, other.key1);
        if (cmp != 0) return cmp < 0;
        return std::strcmp(key2, other.key2) < 0;
    }
    bool operator<=(const CompositeKey& other) const {
        return *this < other || *this == other;
    }
    bool operator>(const CompositeKey& other) const {
        int cmp = std::strcmp(key1, other.key1);
        if (cmp != 0) return cmp > 0;
        return std::strcmp(key2, other.key2) > 0;
    }
    bool operator>=(const CompositeKey& other) const {
        return *this > other || *this == other;
    }
    bool empty() const {
        return key1[0] == '\0' && key2[0] == '\0';
    }
};

struct IndexNode {
    bool is_leaf;
    uint16_t num_keys;
    CompositeKey keys[MAX_KEYS];
    union {
        RecordID values[MAX_KEYS];   // Leaf nodes store record locations
        uint32_t children[MAX_KEYS + 1]; // Internal nodes store page IDs
    } ptrs;

    IndexNode();
};

class BPlusTreeIndex {
private:
    DiskManager& disk_manager;
    BufferPoolManager& cache_manager;
    uint32_t root_page_id;

    uint32_t find_leaf_page(uint32_t current_page_id, const CompositeKey& key, std::vector<uint32_t>* path = nullptr);
    void insert_into_parent(uint32_t left_id, const CompositeKey& key, uint32_t right_id, std::vector<uint32_t>& path);
    void split_leaf(uint32_t leaf_id, const CompositeKey& key, const RecordID& value, std::vector<uint32_t>& path);
    void split_internal(uint32_t parent_id, const CompositeKey& key, uint32_t child_id, std::vector<uint32_t>& path);

public:
    BPlusTreeIndex(DiskManager& disk_mgr, BufferPoolManager& cache_mgr, uint32_t root_id);
    ~BPlusTreeIndex() = default;

    DBErrorCode insert(const CompositeKey& key, const RecordID& value);
    DBErrorCode search(const CompositeKey& key, RecordID& value);
    DBErrorCode range_search(const CompositeKey& start_key, const CompositeKey& end_key, std::vector<RecordID>& results); // Injected Bug 4 (OOB Read)
    
    uint32_t get_root_page_id() const { return root_page_id; }
};

} // namespace FenrirDB

#endif // FENRIRDB_INDEX_H
