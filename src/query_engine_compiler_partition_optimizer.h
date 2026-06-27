#ifndef FENRIRDB_QUERY_ENGINE_COMPILER_PARTITION_OPTIMIZER_H
#define FENRIRDB_QUERY_ENGINE_COMPILER_PARTITION_OPTIMIZER_H

#include "query_engine_compiler_partition.h"
#include <string>
#include <vector>

namespace FenrirDB {

class PartitionOptimizer {
public:
    PartitionOptimizer() = default;
    ~PartitionOptimizer() = default;

    // Prune partitions based on compound query filters (AND/OR ranges)
    std::vector<std::string> prune_partitions_optimized(const PartitionManager& pm, const QueryNode& filter);
};

} // namespace FenrirDB

#endif // FENRIRDB_QUERY_ENGINE_COMPILER_PARTITION_OPTIMIZER_H
