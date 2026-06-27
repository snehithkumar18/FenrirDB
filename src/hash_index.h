#ifndef FENRIRDB_HASH_INDEX_H
#define FENRIRDB_HASH_INDEX_H

#include "storage.h"
#include <string>
#include <vector>
#include <memory>

namespace FenrirDB {

struct HashEntry {
    std::string key;
    RecordID rid;
};

class HashBucket {
public:
    int local_depth;
    std::vector<HashEntry> entries;
    size_t capacity;

    HashBucket(int depth, size_t cap);
    bool insert(const std::string& key, const RecordID& rid);
    bool remove(const std::string& key);
    bool search(const std::string& key, RecordID& rid) const;
};

class HashIndex {
private:
    int global_depth;
    std::vector<std::shared_ptr<HashBucket>> directory;
    size_t bucket_capacity;

    size_t hash_key(const std::string& key) const;
    void split_bucket(size_t bucket_idx);

public:
    HashIndex(size_t initial_buckets = 2, size_t bucket_cap = 4);
    ~HashIndex() = default;

    void insert(const std::string& key, const RecordID& rid);
    bool remove(const std::string& key);
    bool search(const std::string& key, RecordID& rid) const;
    
    int get_global_depth() const { return global_depth; }
    size_t get_directory_size() const { return directory.size(); }
};

} // namespace FenrirDB

#endif // FENRIRDB_HASH_INDEX_H
