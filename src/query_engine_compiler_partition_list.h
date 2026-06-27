#ifndef FENRIRDB_QUERY_ENGINE_COMPILER_PARTITION_LIST_H
#define FENRIRDB_QUERY_ENGINE_COMPILER_PARTITION_LIST_H

#include "database.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace FenrirDB {

struct ListPartition {
    std::string partition_name;
    std::vector<std::string> values;
};

class ListPartitionManager {
private:
    std::string base_table_name;
    std::string partition_col;
    std::vector<ListPartition> partitions;
    std::unordered_map<std::string, std::unique_ptr<Database>> partition_dbs;

public:
    ListPartitionManager(const std::string& table, const std::string& col);
    ~ListPartitionManager() = default;

    void add_partition(const std::string& name, const std::vector<std::string>& vals);
    DBErrorCode insert(const Document& doc);
    std::vector<std::string> prune_partitions(QueryOp op, const Variant& val);
    
    std::string get_partition_col() const { return partition_col; }
};

} // namespace FenrirDB

#endif // FENRIRDB_QUERY_ENGINE_COMPILER_PARTITION_LIST_H
