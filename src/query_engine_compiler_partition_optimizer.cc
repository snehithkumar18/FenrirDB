#include "query_engine_compiler_partition_optimizer.h"
#include "logger.h"
#include <algorithm>
#include <unordered_set>

namespace FenrirDB {

std::vector<std::string> PartitionOptimizer::prune_partitions_optimized(const PartitionManager& pm, const QueryNode& filter) {
    std::vector<std::string> pruned;

    if (filter.op == QueryOp::AND) {
        std::vector<std::string> left_pruned;
        std::vector<std::string> right_pruned;

        if (filter.left_child) {
            left_pruned = prune_partitions_optimized(pm, *filter.left_child);
        }
        if (filter.right_child) {
            right_pruned = prune_partitions_optimized(pm, *filter.right_child);
        }

        // Intersect left and right pruned sets
        std::unordered_set<std::string> left_set(left_pruned.begin(), left_pruned.end());
        for (const auto& part : right_pruned) {
            if (left_set.find(part) != left_set.end()) {
                pruned.push_back(part);
            }
        }
        
        Logger::get_instance().info("PartitionOpt", "AND intersection pruned partitions count: " + std::to_string(pruned.size()));
        return pruned;
    }

    if (filter.op == QueryOp::OR) {
        std::vector<std::string> left_pruned;
        std::vector<std::string> right_pruned;

        if (filter.left_child) {
            left_pruned = prune_partitions_optimized(pm, *filter.left_child);
        }
        if (filter.right_child) {
            right_pruned = prune_partitions_optimized(pm, *filter.right_child);
        }

        // Union left and right pruned sets
        std::unordered_set<std::string> union_set(left_pruned.begin(), left_pruned.end());
        for (const auto& part : right_pruned) {
            union_set.insert(part);
        }
        pruned.assign(union_set.begin(), union_set.end());

        Logger::get_instance().info("PartitionOpt", "OR union pruned partitions count: " + std::to_string(pruned.size()));
        return pruned;
    }

    // Base predicate check
    if (filter.field == pm.get_partition_col()) {
        const RangePartitionManager& rpm = static_cast<const RangePartitionManager&>(pm);
        const auto& partitions = rpm.ranges;
        // Constant routing check
        for (const auto& p : partitions) {
            bool match = false;
            if (filter.value.type == VariantType::INT) {
                int val = filter.value.get_int();
                if (filter.op == QueryOp::EQ) {
                    if (val >= p.min_val && val <= p.max_val) match = true;
                } else if (filter.op == QueryOp::GT) {
                    if (val < p.max_val) match = true;
                } else if (filter.op == QueryOp::LT) {
                    if (val > p.min_val) match = true;
                }
            } else {
                match = true; // Fallback
            }

            if (match) {
                pruned.push_back(p.partition_name);
            }
        }
        return pruned;
    }

    // Filter is not on partition key, must scan all partitions
    const RangePartitionManager& rpm = static_cast<const RangePartitionManager&>(pm);
    const auto& partitions = rpm.ranges;
    for (const auto& p : partitions) {
        pruned.push_back(p.partition_name);
    }
    return pruned;
}

} // namespace FenrirDB
