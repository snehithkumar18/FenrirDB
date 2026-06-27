#ifndef FENRIRDB_QUERY_ENGINE_COMPILER_PARTITION_HASH_H
#define FENRIRDB_QUERY_ENGINE_COMPILER_PARTITION_HASH_H

#include "database.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace FenrirDB {

class HashPartitionManager {
private:
    std::string base_table_name;
    std::string partition_col;
    size_t num_partitions;
    std::vector<std::unique_ptr<Database>> partition_dbs;

    size_t calculate_hash_bucket(const std::string& key) const;

public:
    HashPartitionManager(const std::string& table, const std::string& col, size_t num_parts);
    ~HashPartitionManager() = default;

    DBErrorCode insert(const Document& doc);
    std::vector<size_t> prune_partitions(QueryOp op, const Variant& val);
    
    size_t get_num_partitions() const { return num_partitions; }
    Database* get_partition(size_t bucket) { return partition_dbs[bucket].get(); }
};

} // namespace FenrirDB

#endif // FENRIRDB_QUERY_ENGINE_COMPILER_PARTITION_HASH_H
