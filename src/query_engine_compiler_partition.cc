#include "query_engine_compiler_partition.h"
#include "logger.h"

namespace FenrirDB {

PartitionManager::PartitionManager(const std::string& table, const std::string& col)
    : base_table_name(table), partition_col(col) {
    Logger::get_instance().info("Partition", "PartitionManager initialized on table: " + table + ", column: " + col);
}

void PartitionManager::add_partition(const std::string& name, int min_v, int max_v) {
    ranges.push_back({name, min_v, max_v});
    
    std::string db_file = base_table_name + "_" + name + ".db";
    auto db = std::make_unique<Database>();
    db->open(db_file);
    partition_dbs[name] = std::move(db);

    Logger::get_instance().info("Partition", "Added range partition " + name + " [" + std::to_string(min_v) + ", " + std::to_string(max_v) + "]");
}

DBErrorCode PartitionManager::insert(const Document& doc) {
    Variant v;
    if (!doc.get_field(partition_col, v) || v.type != VariantType::INT) {
        return DBErrorCode::ERR_INVALID_PARAMETER;
    }

    int val = v.get_int();
    std::string matched_partition = "";
    for (const auto& r : ranges) {
        if (val >= r.min_val && val <= r.max_val) {
            matched_partition = r.partition_name;
            break;
        }
    }

    if (matched_partition.empty()) {
        return DBErrorCode::ERR_GENERIC; // No partition matches range
    }

    // Insert into partition database
    Variant id_v;
    std::string key = "doc_";
    if (doc.get_field("id", id_v)) {
        if (id_v.type == VariantType::INT) key += std::to_string(id_v.get_int());
        else if (id_v.type == VariantType::STRING) key += id_v.get_string();
    }

    Logger::get_instance().info("Partition", "Routing insert to partition: " + matched_partition);
    return partition_dbs[matched_partition]->insert(key, doc);
}

std::vector<std::string> PartitionManager::prune_partitions(QueryOp op, const Variant& val) {
    std::vector<std::string> target_partitions;
    if (val.type != VariantType::INT) {
        // Can't prune without integer boundaries
        for (const auto& r : ranges) {
            target_partitions.push_back(r.partition_name);
        }
        return target_partitions;
    }

    int compare_val = val.get_int();
    for (const auto& r : ranges) {
        bool match = false;
        if (op == QueryOp::EQ) {
            if (compare_val >= r.min_val && compare_val <= r.max_val) match = true;
        } else if (op == QueryOp::GT) {
            if (compare_val < r.max_val) match = true;
        } else if (op == QueryOp::LT) {
            if (compare_val > r.min_val) match = true;
        }

        if (match) {
            target_partitions.push_back(r.partition_name);
        }
    }

    Logger::get_instance().info("Partition", "Pruned partitions count: " + std::to_string(ranges.size() - target_partitions.size()));
    return target_partitions;
}

} // namespace FenrirDB
