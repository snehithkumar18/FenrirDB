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

struct IndexNode {
    bool is_leaf;
    uint16_t num_keys;
    char keys[MAX_KEYS][32]; // Fixed-size keys of 32 bytes
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

    uint32_t find_leaf_page(uint32_t current_page_id, const std::string& key, std::vector<uint32_t>* path = nullptr);
    void insert_into_parent(uint32_t left_id, const std::string& key, uint32_t right_id, std::vector<uint32_t>& path);
    void split_leaf(uint32_t leaf_id, const std::string& key, const RecordID& value, std::vector<uint32_t>& path);
    void split_internal(uint32_t parent_id, const std::string& key, uint32_t child_id, std::vector<uint32_t>& path);

public:
    BPlusTreeIndex(DiskManager& disk_mgr, BufferPoolManager& cache_mgr, uint32_t root_id);
    ~BPlusTreeIndex() = default;

    DBErrorCode insert(const std::string& key, const RecordID& value);
    DBErrorCode search(const std::string& key, RecordID& value);
    DBErrorCode range_search(const std::string& start_key, const std::string& end_key, std::vector<RecordID>& results); // Injected Bug 4 (OOB Read)
    
    uint32_t get_root_page_id() const { return root_page_id; }
};

} // namespace FenrirDB

#endif // FENRIRDB_INDEX_H
