#include "hash_index.h"
#include "logger.h"
#include <algorithm>

namespace FenrirDB {

HashBucket::HashBucket(int depth, size_t cap) : local_depth(depth), capacity(cap) {}

bool HashBucket::insert(const std::string& key, const RecordID& rid) {
    for (const auto& entry : entries) {
        if (entry.key == key) {
            return false; // Key already exists
        }
    }
    if (entries.size() >= capacity) {
        return false; // Bucket is full
    }
    entries.push_back({key, rid});
    return true;
}

bool HashBucket::remove(const std::string& key) {
    auto it = std::remove_if(entries.begin(), entries.end(),
                             [&key](const HashEntry& entry) { return entry.key == key; });
    if (it != entries.end()) {
        entries.erase(it, entries.end());
        return true;
    }
    return false;
}

bool HashBucket::search(const std::string& key, RecordID& rid) const {
    for (const auto& entry : entries) {
        if (entry.key == key) {
            rid = entry.rid;
            return true;
        }
    }
    return false;
}

HashIndex::HashIndex(size_t initial_buckets, size_t bucket_cap)
    : global_depth(1), bucket_capacity(bucket_cap) {
    directory.resize(initial_buckets);
    for (size_t i = 0; i < directory.size(); ++i) {
        directory[i] = std::make_shared<HashBucket>(1, bucket_capacity);
    }
    Logger::get_instance().info("HashIndex", "Extensible Hash Index initialized. Global depth: 1");
}

size_t HashIndex::hash_key(const std::string& key) const {
    unsigned long hash = 5381;
    for (char c : key) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

void HashIndex::split_bucket(size_t bucket_idx) {
    auto target_bucket = directory[bucket_idx];
    int local_depth = target_bucket->local_depth;

    if (local_depth == global_depth) {
        // Directory doubling phase
        size_t old_size = directory.size();
        directory.resize(old_size * 2);
        for (size_t i = 0; i < old_size; ++i) {
            directory[i + old_size] = directory[i];
        }
        global_depth++;
        Logger::get_instance().info("HashIndex", "Directory doubled. Global depth: " + std::to_string(global_depth));
    }

    // Split the bucket
    int new_local_depth = local_depth + 1;
    target_bucket->local_depth = new_local_depth;

    auto new_bucket = std::make_shared<HashBucket>(new_local_depth, bucket_capacity);

    // Re-hash and distribute entries
    std::vector<HashEntry> old_entries = std::move(target_bucket->entries);
    target_bucket->entries.clear();

    size_t split_mask = 1 << local_depth;
    for (const auto& entry : old_entries) {
        size_t hash_val = hash_key(entry.key);
        if (hash_val & split_mask) {
            new_bucket->entries.push_back(entry);
        } else {
            target_bucket->entries.push_back(entry);
        }
    }

    // Update directory pointers
    // Every index in directory matching (i & (split_mask - 1) == bucket_idx & (split_mask - 1))
    // needs to point to either old or new bucket based on split_mask bit.
    size_t dir_size = directory.size();
    for (size_t i = 0; i < dir_size; ++i) {
        if ((i & ((1 << local_depth) - 1)) == (bucket_idx & ((1 << local_depth) - 1))) {
            if (i & split_mask) {
                directory[i] = new_bucket;
            } else {
                directory[i] = target_bucket;
            }
        }
    }

    Logger::get_instance().info("HashIndex", "Bucket split complete. Local depth: " + std::to_string(new_local_depth));
}

void HashIndex::insert(const std::string& key, const RecordID& rid) {
    size_t hash_val = hash_key(key);
    size_t bucket_mask = (1 << global_depth) - 1;
    size_t idx = hash_val & bucket_mask;

    while (!directory[idx]->insert(key, rid)) {
        // Bucket is full, trigger a split and retry
        split_bucket(idx);
        bucket_mask = (1 << global_depth) - 1;
        idx = hash_val & bucket_mask;
    }
}

bool HashIndex::remove(const std::string& key) {
    size_t hash_val = hash_key(key);
    size_t idx = hash_val & ((1 << global_depth) - 1);
    return directory[idx]->remove(key);
}

bool HashIndex::search(const std::string& key, RecordID& rid) const {
    size_t hash_val = hash_key(key);
    size_t idx = hash_val & ((1 << global_depth) - 1);
    return directory[idx]->search(key, rid);
}

} // namespace FenrirDB
