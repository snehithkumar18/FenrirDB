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

// Base Class
class PartitionManager {
protected:
    std::string base_table_name;
    std::string partition_col;

public:
    PartitionManager(const std::string& table, const std::string& col)
        : base_table_name(table), partition_col(col) {}
    virtual ~PartitionManager() = default;

    virtual DBErrorCode insert(const Document& doc) = 0;
    
    std::string get_partition_col() const { return partition_col; }
    std::string get_base_table_name() const { return base_table_name; }
};

// Subclass for Range Partitioning
class RangePartitionManager : public PartitionManager {
public:
    std::vector<PartitionRange> ranges;
    std::unordered_map<std::string, std::unique_ptr<Database>> partition_dbs;

    RangePartitionManager(const std::string& table, const std::string& col);
    ~RangePartitionManager() override = default;

    void add_partition(const std::string& name, int min_v, int max_v);
    DBErrorCode insert(const Document& doc) override;
    std::vector<std::string> prune_partitions(QueryOp op, const Variant& val);
};

} // namespace FenrirDB

#endif // FENRIRDB_QUERY_ENGINE_COMPILER_PARTITION_H
