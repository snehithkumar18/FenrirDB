#ifndef FENRIRDB_QUERY_ENGINE_COMPILER_PARTITION_H
#define FENRIRDB_QUERY_ENGINE_COMPILER_PARTITION_H

#include "database.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace FenrirDB {

struct PartitionRange {
    std::string partition_name;
    int min_val;
    int max_val;
};

class PartitionManager {
private:
    std::string base_table_name;
    std::string partition_col;
    std::vector<PartitionRange> ranges;
    std::unordered_map<std::string, std::unique_ptr<Database>> partition_dbs;

public:
    PartitionManager(const std::string& table, const std::string& col);
    ~PartitionManager() = default;

    void add_partition(const std::string& name, int min_v, int max_v);
    DBErrorCode insert(const Document& doc);
    std::vector<std::string> prune_partitions(QueryOp op, const Variant& val);
    
    std::string get_partition_col() const { return partition_col; }
    std::vector<PartitionRange> get_partitions() const { return ranges; }
};

} // namespace FenrirDB

#endif // FENRIRDB_QUERY_ENGINE_COMPILER_PARTITION_H
